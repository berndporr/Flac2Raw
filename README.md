# Flac to Raw

This Android package converts a flac file to a raw file. The output format is 16 bit / little
endian without any header. Just raw audio. Supported sampling rates are 8kHz, 44.1kHz and 48kHz.

It uses the C high performance audio stack of Android so the only limiting factor is the writing
speed of the memory card.

## Usage

### Compile the library

Select `Build` -> `Build variant` and select `release`. Then compile the library.

### Importing the library into your project

In your project do `File` -> `New` -> `New Module` -> `Import AAR`. Import the library from
`Flac2Raw/flac2raw/build/outputs/aar/flac2raw-release.aar`.

Add to your gradle config file the line: `implementation project(":flac2raw-release")`.

### JAVA code

It's implemented as a class with a single method. Instantiating the class loads the
shared library into memory and then you can call `uncompressFile2File` which converts the
audio. The call is blocking.

```
// instantiate the converter
Flac2Raw flac2Raw = new Flac2Raw();
// run it
flac2Raw.uncompressFile2File(getFullPath(audioFileName+".flac"),getFullPath(audioFileName+".raw"),48000);
```

## Unit test
The unit test `UncompressFlacFileTest.java` contains a full example. 
Place a mono flac file called `test.flac` which has a sampling rate of 48kHz in the
`Music` folder of your phone. Run the unit test and you should then see a `test.raw`
in the `Music` folder.

## Status

Converts properly flac mono to mono raw (see unit test) but not tested for any other formats. It should also work
with aac and mp3.

## Credit
[Google open source project](https://android.googlesource.com/platform/frameworks/wilhelm/+/master/tests/examples/slesTestDecodeToBuffQueue.cpp)
