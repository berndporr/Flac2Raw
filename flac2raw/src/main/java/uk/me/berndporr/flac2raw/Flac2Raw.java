package uk.me.berndporr.flac2raw;

import android.content.res.AssetManager;

public class Flac2Raw {

    static {
        System.loadLibrary("flac2raw-jni");
    }

    /***
     * Uncompresses a compressed audio file on the
     * to a raw audio file
     * @param flacFile source flac filename
     * @param rawFile destination for the raw filename
     * @param samplingRateHz sampling rate of the source file
     * @return returns zero on success or the error number
     */
    public native int uncompressFile2File(String flacFile,
                                          String rawFile,
                                          int samplingRateHz);

    /***
     * Uncompresses an Android asset from the "assets" folder
     * to a raw header-less audio file
     * @param assetManager
     * @param flacFile
     * @param rawFile
     * @param samplingRateHz
     * @return returns zero on success or the error number
     */
    public native int uncompressAsset2File(AssetManager assetManager,
                                           String flacFile,
                                           String rawFile,
                                           int samplingRateHz);
}
