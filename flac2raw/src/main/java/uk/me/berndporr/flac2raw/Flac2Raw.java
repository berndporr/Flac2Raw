package uk.me.berndporr.flac2raw;

public class Flac2Raw {

    static {
        System.loadLibrary("flac2raw-jni");
    }

    public native int convertFile2File(String flacFile, String rawFile);

}
