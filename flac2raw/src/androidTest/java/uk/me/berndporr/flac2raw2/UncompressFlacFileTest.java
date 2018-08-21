package uk.me.berndporr.flac2raw2;

import android.Manifest;
import android.app.Activity;
import android.app.UiAutomation;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.support.test.InstrumentationRegistry;
import android.support.test.runner.AndroidJUnit4;
import android.support.v4.app.ActivityCompat;
import android.util.Log;

import org.junit.Test;
import org.junit.runner.RunWith;

import java.io.File;

import static org.junit.Assert.*;


/**
 * Instrumented test, which will execute on an Android device.
 *
 * This test converts a flac file in "/sdcard/Music/test.flac" into a raw file at 48kHz sampling
 * rate. You need to copy test.flac into this directory.
 */
@RunWith(AndroidJUnit4.class)
public class UncompressFlacFileTest {

    static final String audioFileName = "test";

    public class PermissionRequester {

        final String TAG = PermissionRequester.class.getSimpleName();

        void request(String... permissions) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                UiAutomation auto = InstrumentationRegistry.getInstrumentation().getUiAutomation();
                String cmd = "pm grant " + InstrumentationRegistry.getTargetContext().getPackageName() + " %1$s";
                String cmdTest = "pm grant " + InstrumentationRegistry.getContext().getPackageName() + " %1$s";
                for (String perm : permissions) {
                    execute(String.format(cmd, perm), auto);
                    execute(String.format(cmdTest, perm), auto);
                }
            }
        }

        void execute(String currCmd, UiAutomation auto){
            Log.d(TAG, "exec cmd: " + currCmd);
            auto.executeShellCommand(currCmd);
        }
    }


    void askPerm() {
        // asks automatically for permissions without a user dialogue
        PermissionRequester permissionRequester = new PermissionRequester();
        permissionRequester.request(Manifest.permission.WRITE_EXTERNAL_STORAGE, Manifest.permission.READ_EXTERNAL_STORAGE);
        int grantResult = ActivityCompat.checkSelfPermission(InstrumentationRegistry.getTargetContext(), Manifest.permission.WRITE_EXTERNAL_STORAGE);
        assertEquals(PackageManager.PERMISSION_GRANTED, grantResult);
        grantResult = ActivityCompat.checkSelfPermission(InstrumentationRegistry.getContext(), Manifest.permission.WRITE_EXTERNAL_STORAGE);
        assertEquals(PackageManager.PERMISSION_GRANTED, grantResult);
        }

    // subdir where all files are stored
    private static final String TEST_SUBDIR = "Music";

    // full path to the directory where everything is stored
    public static final File TEST_DIR =
            new File(Environment.getExternalStorageDirectory().getPath(), TEST_SUBDIR);

    public static String getFullPath(String filename) {
        File f = new File(TEST_DIR, filename);
        return f.getAbsolutePath();
    }

    private void updateFileSystem(Context context) {
        // update the filesystem
        Intent mediaScanIntent = new Intent(Intent.ACTION_MEDIA_SCANNER_SCAN_FILE);
        Uri contentUri = Uri.fromFile(new File(getFullPath(audioFileName+".raw")));
        mediaScanIntent.setData(contentUri);
        context.sendBroadcast(mediaScanIntent);
    }


    @Test
    public void useAppContext() {
        // Context of the app under test.
        Context appContext = InstrumentationRegistry.getTargetContext();
        assertEquals("uk.me.berndporr.flac2raw2.test", appContext.getPackageName());

        askPerm();

        // instantiate the converter
        Flac2Raw flac2Raw = new Flac2Raw();
        // run it
        flac2Raw.convertFile2File(getFullPath(audioFileName+".flac"),getFullPath(audioFileName+".raw"));

        updateFileSystem(appContext);
    }
}
