package uk.me.berndporr.flac2raw2;

public class Flac2Raw {

    static {
        System.loadLibrary("flac2raw-lib");
    }

    private native int convertFile2File(String flacFile, String rawFile);

}
