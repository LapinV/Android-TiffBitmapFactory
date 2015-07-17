using namespace std;

#ifdef __cplusplus
extern "C" {
#endif

#include "NativeTiffBitmapFactory.h"

int colorMask = 0xFF;

int ARGB_8888 = 5;
int ALPHA_8 = 1;

TIFF *image;
int origwidth = 0;
int origheight = 0;

jobject preferedConfig;

JNIEXPORT jobject

JNICALL Java_org_beyka_tiffbitmapfactory_TiffBitmapFactory_nativeDecodePath
        (JNIEnv *env, jclass clazz, jstring path, jobject options) {

    //Get options
    jclass jBitmapOptionsClass = env->FindClass(
            "org/beyka/tiffbitmapfactory/TiffBitmapFactory$Options");

    jfieldID gOptions_sampleSizeFieldID = env->GetFieldID(jBitmapOptionsClass, "inSampleSize", "I");
    jint inSampleSize = env->GetIntField(options, gOptions_sampleSizeFieldID);

    jfieldID gOptions_justDecodeBoundsFieldID = env->GetFieldID(jBitmapOptionsClass,
                                                                "inJustDecodeBounds", "Z");
    jboolean inJustDecodeBounds = env->GetBooleanField(options, gOptions_justDecodeBoundsFieldID);

    jfieldID gOptions_DirectoryCountFieldID = env->GetFieldID(jBitmapOptionsClass,
                                                              "inDirectoryCount",
                                                              "I");
    jint directoryCount = env->GetIntField(options, gOptions_DirectoryCountFieldID);

    jfieldID gOptions_PreferedConfigFieldID = env->GetFieldID(jBitmapOptionsClass,
                                                              "inPreferredConfig",
                                                              "Landroid/graphics/Bitmap$Config;");
    jobject config = env->GetObjectField(options, gOptions_PreferedConfigFieldID);
    if (config == NULL) {
        LOGI("config is NULL, creating default options");
        jclass bitmapConfig = env->FindClass("android/graphics/Bitmap$Config");
        jfieldID argb8888FieldID = env->GetStaticFieldID(bitmapConfig, "ARGB_8888",
                                                         "Landroid/graphics/Bitmap$Config;");
        config = env->GetStaticObjectField(bitmapConfig, argb8888FieldID);
        env->DeleteLocalRef(bitmapConfig);
    }
    preferedConfig = env->NewGlobalRef(config);

    env->DeleteLocalRef(config);


    //if directory number < 1 set it to 1
    if (directoryCount < 1) directoryCount = 1;

    env->DeleteLocalRef(jBitmapOptionsClass);

    //Open image and read data;
    const char *strPath = NULL;
    strPath = env->GetStringUTFChars(path, 0);
    LOGIS("nativeTiffOpen", strPath);

    image = TIFFOpen(strPath, "r");
    env->ReleaseStringUTFChars(path, strPath);
    if (image == NULL) {
        LOGES("Can\'t open bitmap", strPath);
        return NULL;
    }

    TIFFGetField(image, TIFFTAG_IMAGEWIDTH, &origwidth);
    TIFFGetField(image, TIFFTAG_IMAGELENGTH, &origheight);

    LOGII("Width", origwidth);
    LOGII("Height", origheight);

    jobject java_bitmap = NULL;
    //If need only bounds - return blank bitmap
    if (inJustDecodeBounds) {
        jclass jBitmapOptionsClass = env->FindClass(
                "org/beyka/tiffbitmapfactory/TiffBitmapFactory$Options");
        jfieldID gOptions_outWidthFieldId = env->GetFieldID(jBitmapOptionsClass, "outWidth", "I");
        env->SetIntField(options, gOptions_outWidthFieldId, origwidth);

        jfieldID gOptions_outHeightFieldId = env->GetFieldID(jBitmapOptionsClass, "outHeight", "I");
        env->SetIntField(options, gOptions_outHeightFieldId, origheight);

        jfieldID gOptions_outDirectoryCountFieldId = env->GetFieldID(jBitmapOptionsClass,
                                                                     "outDirectoryCount", "I");
        int dircount = getDyrectoryCount();
        env->SetIntField(options, gOptions_outDirectoryCountFieldId, dircount);

        env->DeleteLocalRef(jBitmapOptionsClass);
    } else {
        java_bitmap = createBitmap(env, inSampleSize, directoryCount, options);
    }

    releaseImage(env);

    return java_bitmap;
}

jobject createBitmap(JNIEnv *env, int inSampleSize, int directoryNumber, jobject options) {
    //Read Config from options. Use nativeInt field from Config class
    jclass configClass = env->FindClass("android/graphics/Bitmap$Config");
    jfieldID nativeIntFieldID = env->GetFieldID(configClass, "nativeInt", "I");
    jint configInt = env->GetIntField(preferedConfig, nativeIntFieldID);
    if (configInt != ARGB_8888 && configInt != ALPHA_8) {
        //TODO Drop exception
        LOGE("Selected Config not supported yet");
        return NULL;
    }


    int origBufferSize = origwidth * origheight;

    unsigned int *buffer = (unsigned int *) _TIFFmalloc(origBufferSize * sizeof(unsigned int));

    if (buffer == NULL) {
        LOGE("Can\'t allocate memory for buffer");
        return NULL;
    }

    //Go to directory
    int dirRead = 1;
    while (dirRead < directoryNumber) {
        TIFFReadDirectory(image);
        dirRead++;
    }

    TIFFReadRGBAImageOriented(image, origwidth, origheight, buffer, ORIENTATION_TOPLEFT, 0);

    // Convert ABGR to ARGB
    int i = 0;
    int j = 0;
    int tmp = 0;
    for (i = 0; i < origheight; i++) {
        for (j = 0; j < origwidth; j++) {
            tmp = buffer[j + origwidth * i];
            buffer[j + origwidth * i] =
                    (tmp & 0xff000000) | ((tmp & 0x00ff0000) >> 16) | (tmp & 0x0000ff00) |
                    ((tmp & 0xff) << 16);
        }
    }

    int bitmapwidth = origwidth;
    int bitmapheight = origheight;

    void *bytes = NULL;
    if (configInt == ARGB_8888) {
        bytes = createBitmapARGB8888(env, inSampleSize, buffer, &bitmapwidth, &bitmapheight);
    } else if (configInt == ALPHA_8) {
        bytes = createBitmapAlpha8(env, inSampleSize, buffer, &bitmapwidth, &bitmapheight);
    }

    //Create mutable bitmap
    jclass bitmapClass = env->FindClass("android/graphics/Bitmap");
    jmethodID methodid = env->GetStaticMethodID(bitmapClass, "createBitmap",
                                                "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;");

    jobject java_bitmap = env->CallStaticObjectMethod(bitmapClass, methodid, bitmapwidth,
                                                      bitmapheight, preferedConfig);

    //Copy data to bitmap
    int ret;
    void *bitmapPixels;
    if ((ret = AndroidBitmap_lockPixels(env, java_bitmap, &bitmapPixels)) < 0) {
        //error
        LOGE("Lock pixels failed");
        return NULL;
    }
    int pixelsCount = bitmapwidth * bitmapheight;

    if (configInt == ARGB_8888) {
        memcpy(bitmapPixels, (jint *) bytes, sizeof(jint) * pixelsCount);
    } else if (configInt == ALPHA_8) {
        memcpy(bitmapPixels, (jbyte *) bytes, sizeof(jbyte) * pixelsCount);
    }

    AndroidBitmap_unlockPixels(env, java_bitmap);

    //remove array
    if (configInt == ARGB_8888) {
        delete[] (jint *) bytes;
    } else if (configInt == ALPHA_8) {
        delete[] (jbyte *) bytes;
    }

    //remove memory
    env->DeleteLocalRef(bitmapClass);

    //Fill options
    jclass jBitmapOptionsClass = env->FindClass(
            "org/beyka/tiffbitmapfactory/TiffBitmapFactory$Options");
    jfieldID gOptions_outWidthFieldId = env->GetFieldID(jBitmapOptionsClass, "outWidth", "I");
    env->SetIntField(options, gOptions_outWidthFieldId, bitmapwidth);

    jfieldID gOptions_outHeightFieldId = env->GetFieldID(jBitmapOptionsClass, "outHeight", "I");
    env->SetIntField(options, gOptions_outHeightFieldId, bitmapheight);

    jfieldID gOptions_outDirectoryCountFieldId = env->GetFieldID(jBitmapOptionsClass,
                                                                 "outDirectoryCount", "I");
    env->SetIntField(options, gOptions_outDirectoryCountFieldId, 0);

    env->DeleteLocalRef(jBitmapOptionsClass);

    return java_bitmap;
}

jint *createBitmapARGB8888(JNIEnv *env, int inSampleSize, unsigned int *buffer, int *bitmapwidth,
                           int *bitmapheight) {
    jint *bytes = NULL;
    if (inSampleSize > 1) {
        *bitmapwidth = origwidth / inSampleSize;
        *bitmapheight = origheight / inSampleSize;
        int bufferSize = *bitmapwidth * *bitmapheight;
        bytes = (jint *) malloc(sizeof(jint) * bufferSize);
        if (bytes == NULL) {
            LOGE("Can\'t allocate memory for temp buffer");
            return NULL;
        }
        else {
            for (int i = 0, i1 = 0; i < *bitmapwidth; i++, i1 += inSampleSize) {
                for (int j = 0, j1 = 0; j < *bitmapheight; j++, j1 += inSampleSize) {

                    //Apply filter to pixel
                    jint crPix = buffer[j1 * origwidth + i1];
                    int sum = 1;

                    int alpha = colorMask & crPix >> 24;
                    int red = colorMask & crPix >> 16;
                    int green = colorMask & crPix >> 8;
                    int blue = colorMask & crPix;

                    //using kernel 3x3

                    //topleft
                    if (i1 - 1 >= 0 && j1 - 1 >= 0) {
                        crPix = buffer[(j1 - 1) * origwidth + i1 - 1];
                        red += colorMask & crPix >> 16;
                        green += colorMask & crPix >> 8;
                        blue += colorMask & crPix;
                        alpha += colorMask & crPix >> 24;
                        sum++;
                    }
                    //top
                    if (j1 - 1 >= 0) {
                        crPix = buffer[(j1 - 1) * origwidth + i1];
                        red += colorMask & crPix >> 16;
                        green += colorMask & crPix >> 8;
                        blue += colorMask & crPix;
                        alpha += colorMask & crPix >> 24;
                        sum++;
                    }
                    // topright
                    if (i1 + 1 < origwidth && j1 - 1 >= 0) {
                        crPix = buffer[(j1 - 1) * origwidth + i1 + 1];
                        red += colorMask & crPix >> 16;
                        green += colorMask & crPix >> 8;
                        blue += colorMask & crPix;
                        alpha += colorMask & crPix >> 24;
                        sum++;
                    }
                    //right
                    if (i1 + 1 < origwidth) {
                        crPix = buffer[j1 * origwidth + i1 + 1];
                        red += colorMask & crPix >> 16;
                        green += colorMask & crPix >> 8;
                        blue += colorMask & crPix;
                        alpha += colorMask & crPix >> 24;
                        sum++;
                    }
                    //bottomright
                    if (i1 + 1 < origwidth && j1 + 1 < origheight) {
                        crPix = buffer[(j1 + 1) * origwidth + i1 + 1];
                        red += colorMask & crPix >> 16;
                        green += colorMask & crPix >> 8;
                        blue += colorMask & crPix;
                        alpha += colorMask & crPix >> 24;
                        sum++;
                    }
                    //bottom
                    if (j1 + 1 < origheight) {
                        crPix = buffer[(j1 + 1) * origwidth + i1 + 1];
                        red += colorMask & crPix >> 16;
                        green += colorMask & crPix >> 8;
                        blue += colorMask & crPix;
                        alpha += colorMask & crPix >> 24;
                        sum++;
                    }
                    //bottomleft
                    if (i1 - 1 >= 0 && j1 + 1 < origheight) {
                        crPix = buffer[(j1 + 1) * origwidth + i1 - 1];
                        red += colorMask & crPix >> 16;
                        green += colorMask & crPix >> 8;
                        blue += colorMask & crPix;
                        alpha += colorMask & crPix >> 24;
                        sum++;
                    }
                    //left
                    if (i1 - 1 >= 0) {
                        crPix = buffer[j1 * origwidth + i1 - 1];
                        red += colorMask & crPix >> 16;
                        green += colorMask & crPix >> 8;
                        blue += colorMask & crPix;
                        alpha += colorMask & crPix >> 24;
                        sum++;
                    }

                    red /= sum;
                    if (red > 255) red = 255;
                    if (red < 0) red = 0;

                    green /= sum;
                    if (green > 255) green = 255;
                    if (green < 0) green = 0;

                    blue /= sum;
                    if (blue > 255) blue = 255;
                    if (blue < 0) blue = 0;

                    alpha /= sum;///= sum;
                    if (alpha > 255) alpha = 255;
                    if (alpha < 0) alpha = 0;

                    crPix = (alpha << 24) | (red << 16) | (green << 8) | (blue);

                    bytes[j * *bitmapwidth + i] = crPix;
                }
            }
        }
    }
    else {
        int bufferSize = *bitmapwidth * *bitmapheight;
        bytes = (jint *) malloc(sizeof(jint) * bufferSize);
        memcpy(bytes, buffer, bufferSize * sizeof(jint));
    }

    //Close Buffer
    if (buffer) {
        _TIFFfree(buffer);
        buffer = NULL;
    }
    return bytes;
}

jbyte *createBitmapAlpha8(JNIEnv *env, int inSampleSize, unsigned int *buffer, int *bitmapwidth,
                          int *bitmapheight) {
    jbyte *bytes = NULL;
//    if (inSampleSize > 1) {
    *bitmapwidth = origwidth / inSampleSize;
    *bitmapheight = origheight / inSampleSize;
    int bufferSize = *bitmapwidth * *bitmapheight;
    bytes = (jbyte *) malloc(sizeof(jbyte) * bufferSize);
    if (bytes == NULL) {
        LOGE("Can\'t allocate memory for temp buffer");
        return NULL;
    }
    else {
        for (int i = 0, i1 = 0; i < *bitmapwidth; i++, i1 += inSampleSize) {
            for (int j = 0, j1 = 0; j < *bitmapheight; j++, j1 += inSampleSize) {

                //Apply filter to pixel
                unsigned int crPix = buffer[j1 * origwidth + i1];
                int sum = 1;

                int alpha = colorMask & crPix >> 24;
//                    int red = colorMask & crPix >> 16;
//                    int green = colorMask & crPix >> 8;
//                    int blue = colorMask & crPix;

                //using kernel 3x3

                //topleft
                if (i1 - 1 >= 0 && j1 - 1 >= 0) {
                    crPix = buffer[(j1 - 1) * origwidth + i1 - 1];
//                        red += colorMask & crPix >> 16;
//                        green += colorMask & crPix >> 8;
//                        blue += colorMask & crPix;
                    alpha += colorMask & crPix >> 24;
                    sum++;
                }
                //top
                if (j1 - 1 >= 0) {
                    crPix = buffer[(j1 - 1) * origwidth + i1];
//                        red += colorMask & crPix >> 16;
//                        green += colorMask & crPix >> 8;
//                        blue += colorMask & crPix;
                    alpha += colorMask & crPix >> 24;
                    sum++;
                }
                // topright
                if (i1 + 1 < origwidth && j1 - 1 >= 0) {
                    crPix = buffer[(j1 - 1) * origwidth + i1 + 1];
//                        red += colorMask & crPix >> 16;
//                        green += colorMask & crPix >> 8;
//                        blue += colorMask & crPix;
                    alpha += colorMask & crPix >> 24;
                    sum++;
                }
                //right
                if (i1 + 1 < origwidth) {
                    crPix = buffer[j1 * origwidth + i1 + 1];
//                        red += colorMask & crPix >> 16;
//                        green += colorMask & crPix >> 8;
//                        blue += colorMask & crPix;
                    alpha += colorMask & crPix >> 24;
                    sum++;
                }
                //bottomright
                if (i1 + 1 < origwidth && j1 + 1 < origheight) {
                    crPix = buffer[(j1 + 1) * origwidth + i1 + 1];
//                        red += colorMask & crPix >> 16;
//                        green += colorMask & crPix >> 8;
//                        blue += colorMask & crPix;
                    alpha += colorMask & crPix >> 24;
                    sum++;
                }
                //bottom
                if (j1 + 1 < origheight) {
                    crPix = buffer[(j1 + 1) * origwidth + i1 + 1];
//                        red += colorMask & crPix >> 16;
//                        green += colorMask & crPix >> 8;
//                        blue += colorMask & crPix;
                    alpha += colorMask & crPix >> 24;
                    sum++;
                }
                //bottomleft
                if (i1 - 1 >= 0 && j1 + 1 < origheight) {
                    crPix = buffer[(j1 + 1) * origwidth + i1 - 1];
//                        red += colorMask & crPix >> 16;
//                        green += colorMask & crPix >> 8;
//                        blue += colorMask & crPix;
                    alpha += colorMask & crPix >> 24;
                    sum++;
                }
                //left
                if (i1 - 1 >= 0) {
                    crPix = buffer[j1 * origwidth + i1 - 1];
//                        red += colorMask & crPix >> 16;
//                        green += colorMask & crPix >> 8;
//                        blue += colorMask & crPix;
                    alpha += colorMask & crPix >> 24;
                    sum++;
                }

//                    red /= sum;
//                    if (red > 255) red = 255;
//                    if (red < 0) red = 0;
//
//                    green /= sum;
//                    if (green > 255) green = 255;
//                    if (green < 0) green = 0;
//
//                    blue /= sum;
//                    if (blue > 255) blue = 255;
//                    if (blue < 0) blue = 0;

                alpha /= sum;///= sum;
                if (alpha > 255) alpha = 255;
                if (alpha < 0) alpha = 0;

                jbyte curPix = alpha;
//                    crPix = (alpha << 24) | (red << 16) | (green << 8) | (blue);

                bytes[j * *bitmapwidth + i] = curPix;
            }
        }
    }
//    }
//    else
//    {
//        int bufferSize = *bitmapwidth * *bitmapheight;
//        bytes = (unsigned int *) malloc(sizeof(unsigned int) * bufferSize);
//        memcpy(bytes, buffer, bufferSize * sizeof(unsigned int));
//    }

    //Close Buffer
    if (buffer) {
        _TIFFfree(buffer);
        buffer = NULL;
    }
    return bytes;
}

jobject createBlankBitmap(JNIEnv *env, int width, int height) {
    jclass bitmapClass = env->FindClass("android/graphics/Bitmap");
    jmethodID methodid = env->GetStaticMethodID(bitmapClass, "createBitmap",
                                                "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;");

    jobject java_bitmap = env->CallStaticObjectMethod(bitmapClass, methodid, width, height,
                                                      preferedConfig);

    env->DeleteLocalRef(bitmapClass);

    return java_bitmap;
}

void releaseImage(JNIEnv * env) {
    if (image) {
        TIFFClose(image);
        image = NULL;
    }

    //Release global ref
    if (preferedConfig) {
        env->DeleteGlobalRef(preferedConfig);
        preferedConfig = NULL;
    }
}

int getDyrectoryCount() {
    int dircount = 0;
    do {
        dircount++;
    } while (TIFFReadDirectory(image));
    return dircount;
}

#ifdef __cplusplus
}
#endif
