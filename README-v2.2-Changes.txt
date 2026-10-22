VideoToMp3 AAR v2.2 - tester (sample app) save + player fix
=============================================================

The ENGINE (AAR) is unchanged from v2.1 - same native library,
the progress fix you already confirmed (1-100%) is in it.
What changed in v2.2 is the SAMPLE / TESTER APP itself; its two
bugs were in the sample code all along:

1) SAVE failed ("Primary directory Download not allowed for
   content://media/external/audio/media"):
   the sample saved through MediaStore's AUDIO collection but
   asked for the Download folder, which that collection rejects.
   It now saves through the DOWNLOADS collection
   (MediaStore.Downloads), which is the one that owns the
   Download folder. No storage permission is needed for this
   on Android 10+.

2) PLAYER stayed at 0:00 / 0:00 with Play disabled:
   the sample only prepared the player AFTER a successful save,
   so the failed save left the player without any file. The
   player now plays the converted MP3 file directly and is
   prepared whether or not the save succeeded.

CONTENTS
--------
  videomp3-release.aar     library, unchanged from v2.1
  VideoToMp3-Sample.apk    FIXED tester app (versionName 2.2,
                           versionCode 3), signed with the same
                           debug keystore as the v2.0/v2.1
                           samples - installs over the old one,
                           no uninstall needed
  examples/                ExampleUsage.java / .kt (unchanged)
  source/                  full source; the app fix is in
                           aar-project/sample/.../MainActivity.java
                           (saveToDownloads + preparePlayer)

(c) Kaalsoft - FFmpeg 6.1.2 / LAME 3.100
