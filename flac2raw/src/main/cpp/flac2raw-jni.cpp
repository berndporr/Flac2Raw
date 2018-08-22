/*
 * Copyright (C) 2011 The Android Open Source Project
 * Copyright (C) 2018 Bernd Porr, mail@berndporr.me.uk
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <fcntl.h>
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

extern "C" {

#define  LOG_TAG    "flac2raw"

// logging
#include <android/log.h>

// convenience wrappers for debugging
#define  LOGD(...)  __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define  LOGE(...)  __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define  LOGV(...)  __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__)
#define  LOGW(...)  __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

#define NUM_EXPLICIT_INTERFACES_FOR_PLAYER 3
/* Size of the decode buffer queue */
#define NB_BUFFERS_IN_QUEUE 4
/* Size of each buffer in the queue */
#define BUFFER_SIZE_IN_SAMPLES 1152 // number of samples per MP3 frame
#define BUFFER_SIZE_IN_BYTES   (2*BUFFER_SIZE_IN_SAMPLES)
/* Local storage for decoded audio data */
int8_t pcmData[NB_BUFFERS_IN_QUEUE * BUFFER_SIZE_IN_BYTES];
/* destination for decoded data */
static FILE *gFp;
/* metadata key index for the PCM format information we want to retrieve */
static int channelCountKeyIndex = -1;
static int sampleRateKeyIndex = -1;
/* size of the struct to retrieve the PCM format metadata values: the values we're interested in
 * are SLuint32, but it is saved in the data field of a SLMetadataInfo, hence the larger size.
 * Nate that this size is queried and displayed at l.452 for demonstration/test purposes.
 *  */
#define PCM_METADATA_VALUE_SIZE 32
/* used to query metadata values */
static SLMetadataInfo *pcmMetaData = NULL;
/* we only want to query / display the PCM format once */
static bool formatQueried = false;
/* to signal to the test app the end of the stream to decode has been reached */
bool eos = false;
/* used to detect errors likely to have occured when the OpenSL ES framework fails to open
 * a resource, for instance because a file URI is invalid, or an HTTP server doesn't respond.
 */
#define PREFETCHEVENT_ERROR_CANDIDATE \
        (SL_PREFETCHEVENT_STATUSCHANGE | SL_PREFETCHEVENT_FILLLEVELCHANGE)
//-----------------------------------------------------------------
/* Exits the application if an error is encountered */
#define ExitOnError(x) ExitOnErrorFunc(x,__LINE__)

void ExitOnErrorFunc(SLresult result, int line) {
    if (SL_RESULT_SUCCESS != result) {
        LOGE("Error code %u encountered at line %d, exiting", result, line);
        exit(EXIT_FAILURE);
    }
}

/* Used to signal prefetching failures */
bool prefetchError = false;
//-----------------------------------------------------------------
/* Structure for passing information to callback function */
typedef struct CallbackCntxt_ {
    SLPlayItf playItf;
    SLMetadataExtractionItf metaItf;
    SLuint32 size;
    SLint8 *pDataBase;    // Base address of local audio data storage
    SLint8 *pData;        // Current address of local audio data storage
} CallbackCntxt;

//-----------------------------------------------------------------
void SignalEos() {
    eos = true;
}
//-----------------------------------------------------------------
/* Callback for "prefetch" events, here used to detect audio resource opening errors */
void PrefetchEventCallback(SLPrefetchStatusItf caller, void *pContext __unused, SLuint32 event) {
    SLpermille level = 0;
    SLresult result;
    result = (*caller)->GetFillLevel(caller, &level);
    ExitOnError(result);
    SLuint32 status;
    LOGV("PrefetchEventCallback: received event %u", event);
    result = (*caller)->GetPrefetchStatus(caller, &status);
    ExitOnError(result);
    if ((PREFETCHEVENT_ERROR_CANDIDATE == (event & PREFETCHEVENT_ERROR_CANDIDATE))
        && (level == 0) && (status == SL_PREFETCHSTATUS_UNDERFLOW)) {
        LOGE("PrefetchEventCallback: Error while prefetching data, exiting");
        prefetchError = true;
        SignalEos();
    }
}

/* Callback for "playback" events, i.e. event happening during decoding */
void DecProgressCallback(
        SLPlayItf caller,
        void *pContext __unused,
        SLuint32 event) {
    SLresult result;
    SLmillisecond msec;
    result = (*caller)->GetPosition(caller, &msec);
    ExitOnError(result);
    if (SL_PLAYEVENT_HEADATEND & event) {
        LOGV("SL_PLAYEVENT_HEADATEND current position=%u ms", msec);
        SignalEos();
    }
}
//-----------------------------------------------------------------
/* Callback for decoding buffer queue events */
void DecPlayCallback(
        SLAndroidSimpleBufferQueueItf queueItf,
        void *pContext) {
    CallbackCntxt *pCntxt = (CallbackCntxt *) pContext;
    /* Save the decoded data  */
    if (fwrite(pCntxt->pDataBase, 1, BUFFER_SIZE_IN_BYTES, gFp) < BUFFER_SIZE_IN_BYTES) {
        LOGE("Error writing to output file, signaling EOS");
        SignalEos();
        return;
    }
    /* Increase data pointer by buffer size */
    pCntxt->pData += BUFFER_SIZE_IN_BYTES;
    if (pCntxt->pData >= pCntxt->pDataBase + (NB_BUFFERS_IN_QUEUE * BUFFER_SIZE_IN_BYTES)) {
        pCntxt->pData = pCntxt->pDataBase;
    }
    ExitOnError((*queueItf)->Enqueue(queueItf, pCntxt->pDataBase, BUFFER_SIZE_IN_BYTES));
    // Note: adding a sleep here or any sync point is a way to slow down the decoding, or
    //  synchronize it with some other event, as the OpenSL ES framework will block until the
    //  buffer queue callback return to proceed with the decoding.
    /* Example: query of the decoded PCM format */
    if (formatQueried) {
        return;
    }
    SLresult res = (*pCntxt->metaItf)->GetValue(pCntxt->metaItf, sampleRateKeyIndex,
                                                PCM_METADATA_VALUE_SIZE, pcmMetaData);
    ExitOnError(res);
    // Note: here we could verify the following:
    //         pcmMetaData->encoding == SL_CHARACTERENCODING_BINARY
    //         pcmMetaData->size == sizeof(SLuint32)
    //       but the call was successful for the PCM format keys, so those conditions are implied
    LOGV("sample rate = %dHz, ", *((SLuint32 *) pcmMetaData->data));
    res = (*pCntxt->metaItf)->GetValue(pCntxt->metaItf, channelCountKeyIndex,
                                       PCM_METADATA_VALUE_SIZE, pcmMetaData);
    ExitOnError(res);
    LOGV("channel count = %d", *((SLuint32 *) pcmMetaData->data));
    formatQueried = true;
}
//-----------------------------------------------------------------
/* Decode an audio path by opening a file descriptor on that path  */
void decToBuffQueue(SLObjectItf sl, const char *src, const char *dst, int samplingRateHz) {
    gFp = fopen(dst, "w");
    if (NULL == gFp) {
        LOGE("Could not write to the phone memory");
        ExitOnError(SL_RESULT_RESOURCE_ERROR);
    }
    FILE *fsrc = fopen(src, "r");
    if (fsrc == NULL) {
        LOGE("Could not read from the phone memory: >>%s<<", src);
        ExitOnError(SL_RESULT_RESOURCE_ERROR);
    }
    fclose(fsrc);
    SLresult result;
    SLEngineItf EngineItf;
    /* Objects this application uses: one audio player */
    SLObjectItf player;
    /* Interfaces for the audio player */
    SLAndroidSimpleBufferQueueItf decBuffQueueItf;
    SLPrefetchStatusItf prefetchItf;
    SLPlayItf playItf;
    SLMetadataExtractionItf mdExtrItf;
    /* Source of audio data for the decoding */
    SLDataSource decSource;
    SLDataLocator_URI decUri;
    SLDataFormat_MIME decMime;
    /* Data sink for decoded audio */
    SLDataSink decDest;
    SLDataLocator_AndroidSimpleBufferQueue decBuffQueue;
    SLDataFormat_PCM pcm;
    SLboolean required[NUM_EXPLICIT_INTERFACES_FOR_PLAYER];
    SLInterfaceID iidArray[NUM_EXPLICIT_INTERFACES_FOR_PLAYER];
    /* Get the SL Engine Interface which is implicit */
    result = (*sl)->GetInterface(sl, SL_IID_ENGINE, (void *) &EngineItf);
    ExitOnError(result);
    /* Initialize arrays required[] and iidArray[] */
    for (int i = 0; i < NUM_EXPLICIT_INTERFACES_FOR_PLAYER; i++) {
        required[i] = SL_BOOLEAN_FALSE;
        iidArray[i] = SL_IID_NULL;
    }
    /* allocate memory to receive the PCM format metadata */
    if (!pcmMetaData) {
        pcmMetaData = (SLMetadataInfo *) malloc(PCM_METADATA_VALUE_SIZE);
    }
    formatQueried = false;
    /* ------------------------------------------------------ */
    /* Configuration of the player  */
    /* Request the AndroidSimpleBufferQueue interface */
    required[0] = SL_BOOLEAN_TRUE;
    iidArray[0] = SL_IID_ANDROIDSIMPLEBUFFERQUEUE;
    /* Request the PrefetchStatus interface */
    required[1] = SL_BOOLEAN_TRUE;
    iidArray[1] = SL_IID_PREFETCHSTATUS;
    /* Request the PrefetchStatus interface */
    required[2] = SL_BOOLEAN_TRUE;
    iidArray[2] = SL_IID_METADATAEXTRACTION;
    /* Setup the data source */
    decUri.locatorType = SL_DATALOCATOR_URI;
    decUri.URI = (SLchar *) src;
    decMime.formatType = SL_DATAFORMAT_MIME;
    /*     this is how ignored mime information is specified, according to OpenSL ES spec
     *     in 9.1.6 SLDataFormat_MIME and 8.23 SLMetadataTraversalItf GetChildInfo */
    decMime.mimeType = (SLchar *) NULL;
    decMime.containerType = SL_CONTAINERTYPE_UNSPECIFIED;
    decSource.pLocator = (void *) &decUri;
    decSource.pFormat = (void *) &decMime;
    /* Setup the data sink */
    decBuffQueue.locatorType = SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE;
    decBuffQueue.numBuffers = NB_BUFFERS_IN_QUEUE;
    /*    set up the format of the data in the buffer queue */
    pcm.formatType = SL_DATAFORMAT_PCM;
    // FIXME valid value required but currently ignored
    pcm.numChannels = 1;
    switch (samplingRateHz) {
        case 48000:
            pcm.samplesPerSec = SL_SAMPLINGRATE_48;
            break;
        case 44100:
            pcm.samplesPerSec = SL_SAMPLINGRATE_44_1;
            break;
        case 8000:
            pcm.samplesPerSec = SL_SAMPLINGRATE_8;
            break;
        default:
            pcm.samplesPerSec = SL_SAMPLINGRATE_48;
            break;
    }
    pcm.bitsPerSample = SL_PCMSAMPLEFORMAT_FIXED_16;
    pcm.containerSize = 16;
    pcm.channelMask = SL_SPEAKER_FRONT_CENTER;
    pcm.endianness = SL_BYTEORDER_LITTLEENDIAN;
    decDest.pLocator = (void *) &decBuffQueue;
    decDest.pFormat = (void *) &pcm;
    /* Create the audio player */
    result = (*EngineItf)->CreateAudioPlayer(EngineItf, &player, &decSource, &decDest,
                                             NUM_EXPLICIT_INTERFACES_FOR_PLAYER, iidArray,
                                             required);
    ExitOnError(result);
    LOGV("Player created");
    /* Realize the player in synchronous mode. */
    result = (*player)->Realize(player, SL_BOOLEAN_FALSE);
    ExitOnError(result);
    LOGV("Player realized");
    /* Get the play interface which is implicit */
    result = (*player)->GetInterface(player, SL_IID_PLAY, (void *) &playItf);
    ExitOnError(result);
    result = (*playItf)->SetCallbackEventsMask(playItf, SL_PLAYEVENT_HEADATEND);
    ExitOnError(result);
    result = (*playItf)->RegisterCallback(playItf, DecProgressCallback, NULL);
    ExitOnError(result);
    LOGV("Play callback registered");
    /* Get the buffer queue interface which was explicitly requested */
    result = (*player)->GetInterface(player, SL_IID_ANDROIDSIMPLEBUFFERQUEUE,
                                     (void *) &decBuffQueueItf);
    ExitOnError(result);
    /* Get the prefetch status interface which was explicitly requested */
    result = (*player)->GetInterface(player, SL_IID_PREFETCHSTATUS, (void *) &prefetchItf);
    ExitOnError(result);
    /* Get the metadata extraction interface which was explicitly requested */
    result = (*player)->GetInterface(player, SL_IID_METADATAEXTRACTION, (void *) &mdExtrItf);
    ExitOnError(result);
    /* ------------------------------------------------------ */
    /* Initialize the callback and its context for the decoding buffer queue */
    CallbackCntxt cntxt;
    cntxt.playItf = playItf;
    cntxt.metaItf = mdExtrItf;
    cntxt.pDataBase = (int8_t *) &pcmData;
    cntxt.pData = cntxt.pDataBase;
    cntxt.size = sizeof(pcmData);
    result = (*decBuffQueueItf)->RegisterCallback(decBuffQueueItf, DecPlayCallback, &cntxt);
    ExitOnError(result);
    /* Enqueue buffers to map the region of memory allocated to store the decoded data */
    LOGV("Enqueueing buffer ");
    for (int i = 0; i < NB_BUFFERS_IN_QUEUE; i++) {
        result = (*decBuffQueueItf)->Enqueue(decBuffQueueItf, cntxt.pData, BUFFER_SIZE_IN_BYTES);
        ExitOnError(result);
        cntxt.pData += BUFFER_SIZE_IN_BYTES;
    }
    cntxt.pData = cntxt.pDataBase;
    /* ------------------------------------------------------ */
    /* Initialize the callback for prefetch errors, if we can't open the resource to decode */
    result = (*prefetchItf)->RegisterCallback(prefetchItf, PrefetchEventCallback, &prefetchItf);
    ExitOnError(result);
    result = (*prefetchItf)->SetCallbackEventsMask(prefetchItf, PREFETCHEVENT_ERROR_CANDIDATE);
    ExitOnError(result);
    /* ------------------------------------------------------ */
    /* Prefetch the data so we can get information about the format before starting to decode */
    /*     1/ cause the player to prefetch the data */
    result = (*playItf)->SetPlayState(playItf, SL_PLAYSTATE_PAUSED);
    ExitOnError(result);
    /*     2/ block until data has been prefetched */
    SLuint32 prefetchStatus = SL_PREFETCHSTATUS_UNDERFLOW;
    SLuint32 timeOutIndex = 50; // time out prefetching after 5s
    while ((prefetchStatus != SL_PREFETCHSTATUS_SUFFICIENTDATA) && (timeOutIndex > 0) &&
           !prefetchError) {
        usleep(10 * 1000);
        (*prefetchItf)->GetPrefetchStatus(prefetchItf, &prefetchStatus);
        timeOutIndex--;
    }
    if (timeOutIndex == 0 || prefetchError) {
        LOGE("Failure to prefetch data in time, exiting");
        ExitOnError(SL_RESULT_CONTENT_NOT_FOUND);
    }
    /* ------------------------------------------------------ */
    /* Display duration */
    SLmillisecond durationInMsec = SL_TIME_UNKNOWN;
    result = (*playItf)->GetDuration(playItf, &durationInMsec);
    ExitOnError(result);
    if (durationInMsec == SL_TIME_UNKNOWN) {
        LOGV("Content duration is unknown");
    } else {
        LOGV("Content duration is %ums", durationInMsec);
    }
    /* ------------------------------------------------------ */
    /* Display the metadata obtained from the decoder */
    //   This is for test / demonstration purposes only where we discover the key and value sizes
    //   of a PCM decoder. An application that would want to directly get access to those values
    //   can make assumptions about the size of the keys and their matching values (all SLuint32)
    SLuint32 itemCount;
    result = (*mdExtrItf)->GetItemCount(mdExtrItf, &itemCount);
    SLuint32 i, keySize, valueSize;
    SLMetadataInfo *keyInfo, *value;
    for (i = 0; i < itemCount; i++) {
        keyInfo = NULL;
        keySize = 0;
        value = NULL;
        valueSize = 0;
        result = (*mdExtrItf)->GetKeySize(mdExtrItf, i, &keySize);
        ExitOnError(result);
        result = (*mdExtrItf)->GetValueSize(mdExtrItf, i, &valueSize);
        ExitOnError(result);
        keyInfo = (SLMetadataInfo *) malloc(keySize);
        if (NULL != keyInfo) {
            result = (*mdExtrItf)->GetKey(mdExtrItf, i, keySize, keyInfo);
            ExitOnError(result);
            LOGV("key[%d] size=%d, name=%s \tvalue size=%d",
                 i, keyInfo->size, keyInfo->data, valueSize);
            /* find out the key index of the metadata we're interested in */
            if (!strcmp((char *) keyInfo->data, ANDROID_KEY_PCMFORMAT_NUMCHANNELS)) {
                channelCountKeyIndex = i;
            } else if (!strcmp((char *) keyInfo->data, ANDROID_KEY_PCMFORMAT_SAMPLERATE)) {
                sampleRateKeyIndex = i;
            }
            free(keyInfo);
        }
    }
    if (channelCountKeyIndex != -1) {
        LOGV("Key %s is at index %d",
             ANDROID_KEY_PCMFORMAT_NUMCHANNELS, channelCountKeyIndex);
    } else {
        LOGD("Unable to find key %s", ANDROID_KEY_PCMFORMAT_NUMCHANNELS);
    }
    if (sampleRateKeyIndex != -1) {
        LOGV("Key %s is at index %d",
             ANDROID_KEY_PCMFORMAT_SAMPLERATE, sampleRateKeyIndex);
    } else {
        LOGD("Unable to find key %s", ANDROID_KEY_PCMFORMAT_SAMPLERATE);
    }
    /* ------------------------------------------------------ */
    /* Start decoding */
    result = (*playItf)->SetPlayState(playItf, SL_PLAYSTATE_PLAYING);
    ExitOnError(result);
    LOGV("Starting to decode");
    /* Decode until the end of the stream is reached */
    {
        while (!eos) {
            usleep(10 * 1000);
        }
    }
    LOGV("EOS signaled");
    /* ------------------------------------------------------ */
    /* End of decoding */
    /* Stop decoding */
    result = (*playItf)->SetPlayState(playItf, SL_PLAYSTATE_STOPPED);
    ExitOnError(result);
    LOGV("Stopped decoding");
    /* Destroy the AudioPlayer object */
    (*player)->Destroy(player);
    fclose(gFp);
    free(pcmMetaData);
    pcmMetaData = NULL;
}


//-----------------------------------------------------------------
jint
Java_uk_me_berndporr_flac2raw_Flac2Raw_uncompressFile2File(JNIEnv *env,
                                                        jclass,
                                                        jstring fFlac,
                                                        jstring fRaw,
                                                        jint samplingRateHz) {
    SLresult result;
    SLObjectItf sl;
    SLEngineOption EngineOption[] = {
            {(SLuint32) SL_ENGINEOPTION_THREADSAFE, (SLuint32) SL_BOOLEAN_TRUE}
    };
    const char *fFlacUTF = env->GetStringUTFChars(fFlac, NULL);
    const char *fRawUTF = env->GetStringUTFChars(fRaw, NULL);

    result = slCreateEngine(&sl, 1, EngineOption, 0, NULL, NULL);
    ExitOnError(result);
    /* Realizing the SL Engine in synchronous mode. */
    result = (*sl)->Realize(sl, SL_BOOLEAN_FALSE);
    ExitOnError(result);
    decToBuffQueue(sl, fFlacUTF, fRawUTF, samplingRateHz);
    /* Shutdown OpenSL ES */
    (*sl)->Destroy(sl);
    return EXIT_SUCCESS;
}

}