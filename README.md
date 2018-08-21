# Flac to Raw

This Android package converts a flac file to a raw file. The output sampling rate is 48kHz.
It uses the C high performance audio stack of Android so is as fast as it can write to the
internal memory.

## Usage

```
// instantiate the converter
Flac2Raw flac2Raw = new Flac2Raw();
// run it
flac2Raw.convertFile2File(getFullPath(audioFileName+".flac"),getFullPath(audioFileName+".raw"));
```

## Unit test
The unit test `UncompressFlacFileTest.java` contains a full example. 
Place a mono flac file called `test.flac` in the
Music folder of your phone. Run the unit test and you should then see a `test.raw`
in the Music folder.

## Status

alpha: converts properly mono to mono raw but but not tested for any other formats

## Credit
[Google open source project](https://android.googlesource.com/platform/frameworks/wilhelm/+/master/tests/examples/slesTestDecodeToBuffQueue.cpp)
