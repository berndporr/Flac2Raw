# Flac to Raw

This Android package converts a flac file to a raw file.

```
// instantiate the converter
Flac2Raw flac2Raw = new Flac2Raw();
// run it
flac2Raw.convertFile2File(getFullPath(audioFileName+".flac"),getFullPath(audioFileName+".raw"));
```

it uses the underlying high performance audio API of Android and everything is done in C++.

# Status

alpha

