package com.kaalsoft.videomp3.sample;

import android.app.Activity;
import android.content.ContentValues;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.media.MediaPlayer;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.provider.MediaStore;
import android.provider.OpenableColumns;
import android.view.View;
import android.view.WindowManager;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import com.kaalsoft.videomp3.MediaInfo;
import com.kaalsoft.videomp3.Options;
import com.kaalsoft.videomp3.VideoToMp3;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Locale;

/**
 * Full-feature tester for the VideoToMp3 library: every conversion
 * option is adjustable, results are saved to the public Download
 * folder (MediaStore on Android 10+, legacy path below) and can be
 * played back inside the app.
 */
public class MainActivity extends Activity {

    private static final int REQ_PICK = 42;
    private static final int REQ_WRITE_PERM = 7;

    private TextView fileLabel, inputInfo, statusText, outInfo;
    private TextView brLabel, vbrLabel, volLabel, playTime;
    private Spinner presetSp, srSp, threadsSp, encqSp;
    private RadioGroup modeRg, chRg;
    private SeekBar brSeek, vbrSeek, volSeek, playSeek;
    private EditText trimStartEt, trimDurEt, trackEt;
    private EditText titleEt, artistEt, albumEt;
    private Button convertBtn, cancelBtn, playBtn;
    private ProgressBar progressBar;

    private Uri pickedUri;
    private File inputFile;
    private String baseName = "audio";
    private volatile boolean converting;

    private MediaPlayer player;
    private Uri savedUri;
    private File savedFile;
    private String savedDisplayName;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private boolean userSeeking;

    // ---------------------------------------------------------------
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        buildUi();
    }

    private TextView label(String text, boolean bold) {
        TextView t = new TextView(this);
        t.setText(text);
        if (bold) {
            t.setTextSize(16);
            t.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
            t.setPadding(0, 26, 0, 6);
        }
        return t;
    }

    private LinearLayout.LayoutParams wrap() {
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        p.setMargins(0, 4, 0, 4);
        return p;
    }

    private Spinner spinner(String[] items) {
        Spinner s = new Spinner(this);
        s.setAdapter(new ArrayAdapter<>(this,
                android.R.layout.simple_spinner_dropdown_item, items));
        s.setLayoutParams(wrap());
        return s;
    }

    private EditText edit(String hint) {
        EditText e = new EditText(this);
        e.setHint(hint);
        e.setLayoutParams(wrap());
        return e;
    }

    private RadioButton radio(RadioGroup g, String text, boolean checked) {
        RadioButton r = new RadioButton(this);
        r.setText(text);
        r.setId(View.generateViewId());
        g.addView(r);
        if (checked) g.check(r.getId());
        return r;
    }

    private void buildUi() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(32, 24, 32, 24);

        // ----- file picking -----
        Button pickBtn = new Button(this);
        pickBtn.setText("Pick a video file");
        pickBtn.setOnClickListener(v -> {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);
            i.setType("*/*");
            i.putExtra(Intent.EXTRA_MIME_TYPES,
                    new String[]{"video/*", "audio/*"});
            startActivityForResult(i, REQ_PICK);
        });
        root.addView(pickBtn, wrap());

        fileLabel = label("No file selected", false);
        root.addView(fileLabel);
        inputInfo = label("", false);
        root.addView(inputInfo);

        // ----- presets -----
        root.addView(label("Preset", true));
        presetSp = spinner(new String[]{
                "Custom", "Low - 96 kbps", "Standard - 192 kbps",
                "High - 256 kbps", "Very high - 320 kbps",
                "VBR high (q2)", "VBR best (q0)", "Voice mono - 64 kbps"});
        root.addView(presetSp);

        // ----- mode + bitrate -----
        root.addView(label("Mode", true));
        modeRg = new RadioGroup(this);
        modeRg.setOrientation(LinearLayout.HORIZONTAL);
        radio(modeRg, "CBR", true);
        radio(modeRg, "ABR", false);
        radio(modeRg, "VBR", false);
        root.addView(modeRg);

        brLabel = label("Bitrate: 192 kbps", false);
        root.addView(brLabel);
        brSeek = new SeekBar(this);
        brSeek.setMax(320 - 8);
        brSeek.setProgress(192 - 8);
        brSeek.setOnSeekBarChangeListener(simpleSeek((bar, val) ->
                brLabel.setText("Bitrate: " + (val + 8) + " kbps")));
        root.addView(brSeek, wrap());

        vbrLabel = label("VBR quality: 2 (0 = best, 9 = smallest)", false);
        root.addView(vbrLabel);
        vbrSeek = new SeekBar(this);
        vbrSeek.setMax(9);
        vbrSeek.setProgress(2);
        vbrSeek.setOnSeekBarChangeListener(simpleSeek((bar, val) ->
                vbrLabel.setText("VBR quality: " + val +
                        " (0 = best, 9 = smallest)")));
        root.addView(vbrSeek, wrap());

        modeRg.setOnCheckedChangeListener((g, id) -> updateModeEnabled());
        presetSp.setOnItemSelectedListener(new SimpleItem() {
            @Override public void onItem(int pos) { applyPreset(pos); }
        });
        updateModeEnabled();

        // ----- sample rate / channels -----
        root.addView(label("Sample rate", true));
        srSp = spinner(new String[]{"Original", "48000", "44100",
                "32000", "22050", "16000", "11025", "8000"});
        root.addView(srSp);

        root.addView(label("Channels", true));
        chRg = new RadioGroup(this);
        chRg.setOrientation(LinearLayout.HORIZONTAL);
        radio(chRg, "Original", true);
        radio(chRg, "Mono", false);
        radio(chRg, "Stereo", false);
        root.addView(chRg);

        // ----- trim -----
        root.addView(label("Trim (optional, seconds)", true));
        trimStartEt = edit("Start (empty = from beginning)");
        trimStartEt.setInputType(android.text.InputType.TYPE_CLASS_NUMBER
                | android.text.InputType.TYPE_NUMBER_FLAG_DECIMAL);
        root.addView(trimStartEt);
        trimDurEt = edit("Length (empty = to the end)");
        trimDurEt.setInputType(android.text.InputType.TYPE_CLASS_NUMBER
                | android.text.InputType.TYPE_NUMBER_FLAG_DECIMAL);
        root.addView(trimDurEt);

        // ----- volume -----
        root.addView(label("Volume", true));
        volLabel = label("Volume: 100%", false);
        root.addView(volLabel);
        volSeek = new SeekBar(this);
        volSeek.setMax(200);
        volSeek.setProgress(100);
        volSeek.setOnSeekBarChangeListener(simpleSeek((bar, val) ->
                volLabel.setText("Volume: " + val + "%")));
        root.addView(volSeek, wrap());

        // ----- audio track -----
        root.addView(label("Audio track (optional)", true));
        trackEt = edit("Empty = first track; else 0, 1, 2 ...");
        trackEt.setInputType(android.text.InputType.TYPE_CLASS_NUMBER);
        root.addView(trackEt);

        // ----- speed -----
        root.addView(label("Speed engine", true));
        root.addView(label("Threads (parallel chunks)", false));
        threadsSp = spinner(new String[]{"Auto", "Single", "2", "3", "4"});
        root.addView(threadsSp);
        root.addView(label("Encoder quality (LAME algorithm)", false));
        encqSp = spinner(new String[]{"Default", "0 (slowest, best)",
                "1", "2", "3", "4", "5", "6", "7", "8",
                "9 (fastest)"});
        root.addView(encqSp);

        // ----- ID3 -----
        root.addView(label("ID3 tags (optional)", true));
        titleEt = edit("Title");
        root.addView(titleEt);
        artistEt = edit("Artist");
        root.addView(artistEt);
        albumEt = edit("Album");
        root.addView(albumEt);

        // ----- convert -----
        root.addView(label("Convert", true));
        convertBtn = new Button(this);
        convertBtn.setText("Convert to MP3 -> Download folder");
        convertBtn.setOnClickListener(v -> startConvert());
        root.addView(convertBtn, wrap());

        cancelBtn = new Button(this);
        cancelBtn.setText("Cancel");
        cancelBtn.setEnabled(false);
        cancelBtn.setOnClickListener(v -> VideoToMp3.cancel());
        root.addView(cancelBtn, wrap());

        progressBar = new ProgressBar(this, null,
                android.R.attr.progressBarStyleHorizontal);
        progressBar.setMax(100);
        root.addView(progressBar, wrap());

        statusText = label("Library: " + VideoToMp3.version(), false);
        root.addView(statusText);
        outInfo = label("", false);
        root.addView(outInfo);

        // ----- player -----
        root.addView(label("Player", true));
        playBtn = new Button(this);
        playBtn.setText("Play");
        playBtn.setEnabled(false);
        playBtn.setOnClickListener(v -> togglePlay());
        root.addView(playBtn, wrap());
        playSeek = new SeekBar(this);
        playSeek.setOnSeekBarChangeListener(
                new SeekBar.OnSeekBarChangeListener() {
                    @Override public void onProgressChanged(SeekBar b,
                            int p, boolean fromUser) {
                        if (fromUser && player != null)
                            updatePlayTime(p, player.getDuration());
                    }
                    @Override public void onStartTrackingTouch(SeekBar b) {
                        userSeeking = true;
                    }
                    @Override public void onStopTrackingTouch(SeekBar b) {
                        userSeeking = false;
                        if (player != null) player.seekTo(b.getProgress());
                    }
                });
        root.addView(playSeek, wrap());
        playTime = label("0:00 / 0:00", false);
        root.addView(playTime);

        ScrollView scroll = new ScrollView(this);
        scroll.addView(root);
        setContentView(scroll);
    }

    // ---------------------------------------------------------------
    private interface SeekAction { void run(SeekBar bar, int value); }

    private SeekBar.OnSeekBarChangeListener simpleSeek(SeekAction a) {
        return new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar b, int p,
                    boolean fromUser) { a.run(b, p); }
            @Override public void onStartTrackingTouch(SeekBar b) { }
            @Override public void onStopTrackingTouch(SeekBar b) { }
        };
    }

    private abstract static class SimpleItem
            implements android.widget.AdapterView.OnItemSelectedListener {
        public abstract void onItem(int pos);
        @Override public void onItemSelected(
                android.widget.AdapterView<?> parent, View v, int pos,
                long id) { onItem(pos); }
        @Override public void onNothingSelected(
                android.widget.AdapterView<?> parent) { }
    }

    private boolean isVbr() {
        return modeRg.indexOfChild(findViewById(
                modeRg.getCheckedRadioButtonId())) == 2;
    }

    private void updateModeEnabled() {
        boolean vbr = isVbr();
        brSeek.setEnabled(!vbr);
        vbrSeek.setEnabled(vbr);
    }

    private void checkRadio(RadioGroup g, int index) {
        g.check(g.getChildAt(index).getId());
    }

    private void applyPreset(int pos) {
        switch (pos) {
            case 1: checkRadio(modeRg, 0); brSeek.setProgress(96 - 8); break;
            case 2: checkRadio(modeRg, 0); brSeek.setProgress(192 - 8); break;
            case 3: checkRadio(modeRg, 0); brSeek.setProgress(256 - 8); break;
            case 4: checkRadio(modeRg, 0); brSeek.setProgress(320 - 8); break;
            case 5: checkRadio(modeRg, 2); vbrSeek.setProgress(2); break;
            case 6: checkRadio(modeRg, 2); vbrSeek.setProgress(0); break;
            case 7: checkRadio(modeRg, 0); brSeek.setProgress(64 - 8);
                    checkRadio(chRg, 1); break;
            default: break; // Custom: leave everything as-is
        }
        updateModeEnabled();
    }

    // ---------------------------------------------------------------
    @Override
    protected void onActivityResult(int req, int result, Intent data) {
        super.onActivityResult(req, result, data);
        if (req != REQ_PICK || result != RESULT_OK || data == null ||
                data.getData() == null)
            return;
        pickedUri = data.getData();
        new Thread(() -> {
            setStatus("Copying the picked file...");
            try {
                String name = displayName(pickedUri);
                if (name == null) name = "input";
                baseName = name.replaceAll("\\.[^.]+$", "");
                inputFile = copyToCache(pickedUri, name);
                MediaInfo info = VideoToMp3.probe(inputFile
                        .getAbsolutePath());
                final String fname = name;
                ui.post(() -> {
                    fileLabel.setText("File: " + fname);
                    if (info != null) {
                        inputInfo.setText(String.format(Locale.US,
                                "Duration %.1f s | codec %s | %d Hz | " +
                                "%d ch | %d kbps | format %s",
                                info.durationSeconds, info.audioCodec,
                                info.sampleRate, info.channels,
                                info.bitRate / 1000, info.format));
                    } else {
                        inputInfo.setText("Could not probe this file");
                    }
                    if (titleEt.getText().toString().isEmpty())
                        titleEt.setText(baseName);
                    setStatus("Ready");
                });
            } catch (Exception e) {
                setStatus("Copy failed: " + e.getMessage());
            }
        }).start();
    }

    private String displayName(Uri uri) {
        try (Cursor c = getContentResolver().query(uri, null, null,
                null, null)) {
            if (c != null && c.moveToFirst()) {
                int idx = c.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (idx >= 0) return c.getString(idx);
            }
        } catch (Exception ignored) { }
        return null;
    }

    private File copyToCache(Uri uri, String name) throws IOException {
        File out = new File(getCacheDir(),
                "in_" + name.replaceAll("[^a-zA-Z0-9._-]", "_"));
        try (InputStream is = getContentResolver().openInputStream(uri);
             OutputStream os = new FileOutputStream(out)) {
            if (is == null) throw new IOException("Cannot open the file");
            copyStream(is, os);
        }
        return out;
    }

    private static void copyStream(InputStream is, OutputStream os)
            throws IOException {
        byte[] buf = new byte[128 * 1024];
        int n;
        while ((n = is.read(buf)) > 0) os.write(buf, 0, n);
        os.flush();
    }

    // ---------------------------------------------------------------
    private void setStatus(String s) {
        ui.post(() -> statusText.setText(s));
    }

    private double parseDouble(EditText e, double fallback) {
        try {
            String s = e.getText().toString().trim();
            return s.isEmpty() ? fallback : Double.parseDouble(s);
        } catch (NumberFormatException ex) {
            return fallback;
        }
    }

    private int parseInt(EditText e, int fallback) {
        try {
            String s = e.getText().toString().trim();
            return s.isEmpty() ? fallback : Integer.parseInt(s);
        } catch (NumberFormatException ex) {
            return fallback;
        }
    }

    private void startConvert() {
        if (inputFile == null) {
            Toast.makeText(this, "Pick a video file first",
                    Toast.LENGTH_SHORT).show();
            return;
        }
        if (Build.VERSION.SDK_INT < 29 &&
                checkSelfPermission(
                        android.Manifest.permission.WRITE_EXTERNAL_STORAGE)
                        != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{
                    android.Manifest.permission.WRITE_EXTERNAL_STORAGE},
                    REQ_WRITE_PERM);
            return;
        }
        doConvert();
    }

    @Override
    public void onRequestPermissionsResult(int req, String[] perms,
            int[] grants) {
        super.onRequestPermissionsResult(req, perms, grants);
        if (req == REQ_WRITE_PERM) {
            if (grants.length > 0 &&
                    grants[0] == PackageManager.PERMISSION_GRANTED) {
                doConvert();
            } else {
                Toast.makeText(this, "Storage permission is needed to " +
                        "save to Download", Toast.LENGTH_LONG).show();
            }
        }
    }

    private Options buildOptions() {
        Options.Builder b = new Options.Builder();
        int modeIdx = modeRg.indexOfChild(findViewById(
                modeRg.getCheckedRadioButtonId()));
        b.mode(modeIdx == 1 ? Options.Mode.ABR
                : modeIdx == 2 ? Options.Mode.VBR : Options.Mode.CBR);
        b.bitrateKbps(brSeek.getProgress() + 8);
        b.vbrQuality(vbrSeek.getProgress());
        String sr = (String) srSp.getSelectedItem();
        b.sampleRate("Original".equals(sr) ? 0 : Integer.parseInt(sr));
        int chIdx = chRg.indexOfChild(findViewById(
                chRg.getCheckedRadioButtonId()));
        b.channels(chIdx == 1 ? Options.CHANNELS_MONO
                : chIdx == 2 ? Options.CHANNELS_STEREO
                : Options.CHANNELS_KEEP);
        double ts = parseDouble(trimStartEt, -1);
        double td = parseDouble(trimDurEt, -1);
        if (ts >= 0 || td >= 0)
            b.trim(Math.max(ts, 0), Math.max(td, 0));
        b.volumePercent(Math.max(volSeek.getProgress(), 1));
        int track = parseInt(trackEt, -1);
        if (track >= 0) b.audioStream(track);
        String th = (String) threadsSp.getSelectedItem();
        b.threads("Auto".equals(th) ? 0
                : "Single".equals(th) ? 1 : Integer.parseInt(th));
        b.encoderQuality(encqSp.getSelectedItemPosition() - 1);
        String t = titleEt.getText().toString().trim();
        if (!t.isEmpty()) b.title(t);
        String ar = artistEt.getText().toString().trim();
        if (!ar.isEmpty()) b.artist(ar);
        String al = albumEt.getText().toString().trim();
        if (!al.isEmpty()) b.album(al);
        return b.build();
    }

    private void doConvert() {
        converting = true;
        convertBtn.setEnabled(false);
        cancelBtn.setEnabled(true);
        progressBar.setProgress(0);
        outInfo.setText("");
        getWindow().addFlags(
                WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        stopPlayer();

        final Options options = buildOptions();
        final File cacheOut = new File(getCacheDir(),
                "out_" + baseName.replaceAll("[^a-zA-Z0-9._-]", "_") + ".mp3");
        final long started = System.currentTimeMillis();

        new Thread(() -> {
            boolean ok = VideoToMp3.convert(
                    inputFile.getAbsolutePath(),
                    cacheOut.getAbsolutePath(), options,
                    percent -> ui.post(() -> {
                        progressBar.setProgress(percent);
                        statusText.setText("Converting... " + percent + "%");
                    }));
            converting = false;
            ui.post(() -> {
                getWindow().clearFlags(
                        WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
                convertBtn.setEnabled(true);
                cancelBtn.setEnabled(false);
            });
            if (!ok) {
                setStatus("Failed: " + VideoToMp3.lastError());
                return;
            }
            double secs = (System.currentTimeMillis() - started) / 1000.0;
            String outName = baseName + ".mp3";
            String saveError = null;
            try {
                savedUri = saveToDownloads(cacheOut, outName);
            } catch (Exception e) {
                saveError = e.getMessage();
            }
            MediaInfo oi = VideoToMp3.probe(
                    cacheOut.getAbsolutePath());
            StringBuilder info = new StringBuilder();
            if (saveError == null) {
                info.append(String.format(Locale.US,
                        "Saved to Download/%s (%.2f MB) in %.1f s",
                        savedDisplayName != null ? savedDisplayName
                                : outName,
                        cacheOut.length() / 1048576.0, secs));
            } else {
                info.append(String.format(Locale.US,
                        "Converted in %.1f s (%.2f MB)",
                        secs, cacheOut.length() / 1048576.0));
            }
            if (oi != null) {
                info.append(String.format(Locale.US,
                        "\nOutput: %.1f s | %d kbps | %d Hz | %d ch",
                        oi.durationSeconds, oi.bitRate / 1000,
                        oi.sampleRate, oi.channels));
            }
            final String infoText = info.toString();
            final String saveErrText = saveError;
            ui.post(() -> {
                outInfo.setText(infoText);
                setStatus(saveErrText == null ? "Done"
                        : "Converted, but saving to Download failed: "
                                + saveErrText);
                // The player plays the converted file itself, so it
                // works whether or not the save above succeeded.
                preparePlayer(cacheOut);
            });
        }).start();
    }

    /** Copies the MP3 into the public Download folder; returns its Uri. */
    private Uri saveToDownloads(File src, String name) throws IOException {
        if (Build.VERSION.SDK_INT >= 29) {
            // The Download folder belongs to the Downloads collection.
            // (The Audio collection used before rejects RELATIVE_PATH
            // "Download": it only allows Music, Podcasts, Alarms, ...)
            ContentValues cv = new ContentValues();
            cv.put(MediaStore.Downloads.DISPLAY_NAME, name);
            cv.put(MediaStore.Downloads.MIME_TYPE, "audio/mpeg");
            cv.put(MediaStore.Downloads.RELATIVE_PATH,
                    Environment.DIRECTORY_DOWNLOADS);
            cv.put(MediaStore.Downloads.IS_PENDING, 1);
            Uri uri = getContentResolver().insert(
                    MediaStore.Downloads.getContentUri(
                            MediaStore.VOLUME_EXTERNAL_PRIMARY), cv);
            if (uri == null)
                throw new IOException("MediaStore insert failed");
            try (OutputStream os = getContentResolver()
                        .openOutputStream(uri);
                 InputStream is = new FileInputStream(src)) {
                if (os == null)
                    throw new IOException("Cannot open the output");
                copyStream(is, os);
            }
            cv.clear();
            cv.put(MediaStore.Downloads.IS_PENDING, 0);
            getContentResolver().update(uri, cv, null, null);
            savedDisplayName = displayName(uri);
            savedFile = null;
            return uri;
        }
        File dir = Environment.getExternalStoragePublicDirectory(
                Environment.DIRECTORY_DOWNLOADS);
        if (!dir.exists()) dir.mkdirs();
        File dst = new File(dir, name);
        int n = 1;
        while (dst.exists()) {
            dst = new File(dir, name.replace(".mp3", " (" + n + ").mp3"));
            n++;
        }
        try (InputStream is = new FileInputStream(src);
             OutputStream os = new FileOutputStream(dst)) {
            copyStream(is, os);
        }
        savedDisplayName = dst.getName();
        savedFile = dst;
        return Uri.fromFile(dst);
    }

    // ---------------------------------------------------------------
    private void preparePlayer(File mp3) {
        stopPlayer();
        try {
            player = new MediaPlayer();
            player.setDataSource(mp3.getAbsolutePath());
            player.setOnCompletionListener(mp -> {
                playBtn.setText("Play");
                playSeek.setProgress(0);
                updatePlayTime(0, mp.getDuration());
            });
            player.prepare();
            playSeek.setMax(player.getDuration());
            updatePlayTime(0, player.getDuration());
            playBtn.setEnabled(true);
            playBtn.setText("Play");
        } catch (Exception e) {
            playBtn.setEnabled(false);
            setStatus("Player error: " + e.getMessage());
        }
    }

    private void togglePlay() {
        if (player == null) return;
        if (player.isPlaying()) {
            player.pause();
            playBtn.setText("Play");
        } else {
            player.start();
            playBtn.setText("Pause");
            ui.post(playerTicker);
        }
    }

    private final Runnable playerTicker = new Runnable() {
        @Override public void run() {
            if (player != null && player.isPlaying() && !userSeeking) {
                playSeek.setProgress(player.getCurrentPosition());
                updatePlayTime(player.getCurrentPosition(),
                        player.getDuration());
                ui.postDelayed(this, 500);
            }
        }
    };

    private void updatePlayTime(int posMs, int durMs) {
        playTime.setText(fmtTime(posMs) + " / " + fmtTime(durMs));
    }

    private static String fmtTime(int ms) {
        int s = Math.max(ms, 0) / 1000;
        return String.format(Locale.US, "%d:%02d", s / 60, s % 60);
    }

    private void stopPlayer() {
        if (player != null) {
            try { player.stop(); } catch (Exception ignored) { }
            player.release();
            player = null;
        }
        if (playBtn != null) {
            playBtn.setEnabled(false);
            playBtn.setText("Play");
        }
    }

    @Override
    protected void onDestroy() {
        stopPlayer();
        super.onDestroy();
    }
}
