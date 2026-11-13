
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "avs3_options.h"
#include "avs3_cnst_com.h"
#include "avs3_stat_com.h"
#include "avs3_prot_com.h"

static int16_t DestroyModel(NeuralCodecHandle *codecSt);


static int16_t InitEncoder(
	modul_structure *fModel,
    ModelStructHandle encoderHandle,
    int16_t featDimIn
)
{
    int16_t numLayers;
    int16_t featDimInLayer;     // intput feature dim for each layer

    // Number of layers
//    fread(&numLayers, sizeof(int16_t), 1, fModel);
	memcpy(&numLayers, fModel->data + fModel->nIndex, sizeof(int16_t));
	fModel->nIndex += sizeof(int16_t);

    encoderHandle->numLayers = numLayers;
    //(stdout, "encoder num layers: %d\n", encoderHandle->numLayers);

    // init cnn layers
    for (int16_t i = 0; i < MAX_LAYERS; i++) {
        encoderHandle->cnnLayers[i] = NULL;
    }
    for (int16_t i = 0; i < encoderHandle->numLayers; i++) {

        // malloc each layer
        encoderHandle->cnnLayers[i] = (CnnStructHandle)malloc(sizeof(CnnStruct));
        if (encoderHandle->cnnLayers[i] == NULL) {
			LOGD("Malloc encoder cnn layer error!!\n");
            return -1;
        }

        // get input feature dim for each layer
        if (i == 0) {
            featDimInLayer = featDimIn;
        }
        else {
            featDimInLayer = encoderHandle->cnnLayers[i - 1]->featDimOut;
        }

        // init cnn layer
        InitCnnLayer(fModel, encoderHandle->cnnLayers[i], 0, featDimInLayer);
    }

    return 0;
}


static int16_t DestroyEncoder(
    ModelStructHandle encoderHandle
)
{
    if (encoderHandle == NULL) return 0;
	for (int16_t i = 0; i < encoderHandle->numLayers && i < MAX_LAYERS; i++) {
        if (encoderHandle->cnnLayers[i] == NULL) continue;

        DestroyCnnLayer(encoderHandle->cnnLayers[i]);

        free(encoderHandle->cnnLayers[i]);
        encoderHandle->cnnLayers[i] = NULL;
    }

	return 0;
}


static int16_t InitDecoder(
    modul_structure *fModel,
    ModelStructHandle decoderHandle,
    int16_t featDimIn
)
{
    int16_t numLayers;
    int16_t featDimInLayer;     // intput feature dim for each layer

    // Number of layers
//    fread(&numLayers, sizeof(int16_t), 1, fModel);
	memcpy(&numLayers, fModel->data + fModel->nIndex, sizeof(int16_t));
	fModel->nIndex += sizeof(int16_t);
	decoderHandle->numLayers = numLayers;
    //fprintf(stdout, "decoder num layers: %d\n", decoderHandle->numLayers);

    // init cnn layers
    for (int16_t i = 0; i < MAX_LAYERS; i++) {
        decoderHandle->cnnLayers[i] = NULL;
    }
    for (int16_t i = 0; i < decoderHandle->numLayers; i++) {

        // malloc each layer
        decoderHandle->cnnLayers[i] = (CnnStructHandle)malloc(sizeof(CnnStruct));
        if (decoderHandle->cnnLayers[i] == NULL) {
			LOGD("Malloc decoder cnn layer error!!\n");
            return -1;
        }

        // get input feature dim for each layer
        if (i == 0) {
            featDimInLayer = featDimIn;
        }
        else {
            featDimInLayer = decoderHandle->cnnLayers[i - 1]->featDimOut;
        }

        // init cnn layer
        InitCnnLayer(fModel, decoderHandle->cnnLayers[i], 1, featDimInLayer);
    }

    return 0;
}


static int16_t DestroyDecoder(
    ModelStructHandle decoderHandle
)
{
    if (decoderHandle == NULL) return 0;
	for (int16_t i = 0; i < decoderHandle->numLayers && i < MAX_LAYERS; i++) {
        if (decoderHandle->cnnLayers[i] == NULL) continue;

        DestroyCnnLayer(decoderHandle->cnnLayers[i]);

        free(decoderHandle->cnnLayers[i]);
        decoderHandle->cnnLayers[i] = NULL;
    }

	return 0;
}


static int16_t InitContextScale(
    modul_structure *fModel,
    int16_t *numContextScale,
    float **contextScale
)
{
    // get number of context scale
//    fread(numContextScale, sizeof(int16_t), 1, fModel);
	memcpy(numContextScale, fModel->data + fModel->nIndex, sizeof(int16_t));
	fModel->nIndex += sizeof(int16_t);

    // get context scales
    *contextScale = (float *)malloc(sizeof(float) * (*numContextScale));
    if (*contextScale == NULL) {
        LOGD("Malloc context scale error!!\n");
        return -1;
    }
//    fread(*contextScale, sizeof(float), *numContextScale, fModel);
	memcpy(*contextScale, fModel->data + fModel->nIndex, sizeof(float)*(*numContextScale));
	fModel->nIndex += sizeof(float)*(*numContextScale);

    return 0;
}


static int16_t LoadModel(
	modul_structure *fModel,
    NeuralCodecHandle *codecSt,
    int16_t hasContext, 
    int16_t numFeatEncode
)
{
    // Create codec structure
    (*codecSt) = (NeuralCodecHandle)malloc(sizeof(NeuralCodecStruct));
    if ((*codecSt) == NULL) {
		LOGD("Malloc codec structure error!\n");
        return -1;
    }
    memset(*codecSt, 0, sizeof(NeuralCodecStruct));

    // Init input feature dims
    (*codecSt)->numLinesEncode = numFeatEncode;

    // Create encoder structure
    (*codecSt)->encoderHandle = (ModelStructHandle)malloc(sizeof(ModelStruct));
    if ((*codecSt)->encoderHandle == NULL) {
		LOGD("Malloc encoder structure error!\n");
        DestroyModel(codecSt);
        return -1;
    }
    memset((*codecSt)->encoderHandle, 0, sizeof(ModelStruct));
    // Init encoder
    if (InitEncoder(fModel, (*codecSt)->encoderHandle, (*codecSt)->numLinesEncode) != 0) {
        DestroyModel(codecSt);
        return -1;
    }

    // Init output latent dims
    ModelStructHandle encoderHandle = (*codecSt)->encoderHandle;
    if (encoderHandle->numLayers <= 0 || encoderHandle->numLayers > MAX_LAYERS) {
        DestroyModel(codecSt);
        return -1;
    }
    (*codecSt)->numLatentEncode = encoderHandle->cnnLayers[encoderHandle->numLayers - 1]->featDimOut;
    // Init output latent channels
    (*codecSt)->numLatentChannels = encoderHandle->cnnLayers[encoderHandle->numLayers - 1]->numChannelsOut;

    // Create decoder structure
    (*codecSt)->decoderHandle = (ModelStructHandle)malloc(sizeof(ModelStruct));
    if ((*codecSt)->decoderHandle == NULL) {
		LOGD("Malloc decoder structure error!\n");
        DestroyModel(codecSt);
        return -1;
    }
    memset((*codecSt)->decoderHandle, 0, sizeof(ModelStruct));
    // Init decoder
    if (InitDecoder(fModel, (*codecSt)->decoderHandle, (*codecSt)->numLatentEncode) != 0) {
        DestroyModel(codecSt);
        return -1;
    }

    // Create quantizer structure
    (*codecSt)->quantizerHandle = (QuantizerHandle)malloc(sizeof(QuantizerStruct));
    if ((*codecSt)->quantizerHandle == NULL) {
		LOGD("Malloc quantizer handle error!\n");
        DestroyModel(codecSt);
        return -1;
    }
    memset((*codecSt)->quantizerHandle, 0, sizeof(QuantizerStruct));
    // Init quantizer
    if (InitQuantizer(fModel, (*codecSt)->quantizerHandle, (*codecSt)->numLatentChannels) != 0) {
        DestroyModel(codecSt);
        return -1;
    }

    // Init context scale for hyper-prior base model
    (*codecSt)->numContextScale = 0;
    (*codecSt)->contextScale = NULL;
    if (hasContext == 1) {
        if (InitContextScale(fModel, &((*codecSt)->numContextScale), &(*codecSt)->contextScale) != 0) {
            DestroyModel(codecSt);
            return -1;
        }
    }

    // Create range coder structure
    (*codecSt)->rangeCoderConfig = (RangeCoderConfigHandle)malloc(sizeof(RangeCoderConfigStruct));
    if ((*codecSt)->rangeCoderConfig == NULL) {
		LOGD("Malloc range coder handle error!\n");
        DestroyModel(codecSt);
        return -1;
    }
    memset((*codecSt)->rangeCoderConfig, 0, sizeof(RangeCoderConfigStruct));
    // Init range coder config
    if (hasContext == 0) {
        // for vae, numCdf is numLatentChannels
        if (InitRangeCoderConfig(fModel, (*codecSt)->rangeCoderConfig, (*codecSt)->numLatentChannels) != 0) {
            DestroyModel(codecSt);
            return -1;
        }
    }
    else {
        // for hyper, numCdf is numContextScale
        if (InitRangeCoderConfig(fModel, (*codecSt)->rangeCoderConfig, (*codecSt)->numContextScale) != 0) {
            DestroyModel(codecSt);
            return -1;
        }
    }

    return 0;
}


static int16_t DestroyModel(
    NeuralCodecHandle *codecSt
)
{
    NeuralCodecHandle st;

	if (codecSt == NULL || (*codecSt) == NULL) {
		LOGD("Null codec structure in DestroyModel func!!\n");
        return -1;
    }
    st = *codecSt;

    // destroy encoder
    if (st->encoderHandle != NULL) {
        DestroyEncoder(st->encoderHandle);
        free(st->encoderHandle);
        st->encoderHandle = NULL;
    }

	// destroy decoder
    if (st->decoderHandle != NULL) {
        DestroyDecoder(st->decoderHandle);
        free(st->decoderHandle);
        st->decoderHandle = NULL;
    }

	// destroy quantizer
    if (st->quantizerHandle != NULL) {
        DestroyQuantizer(st->quantizerHandle);
        free(st->quantizerHandle);
        st->quantizerHandle = NULL;
    }

	// destroy context scale
    if (st->contextScale != NULL) {
        free(st->contextScale);
        st->contextScale = NULL;
    }

	// destroy range coder
    if (st->rangeCoderConfig != NULL) {
        DestroyRangeCoderConfig(st->rangeCoderConfig);
        free(st->rangeCoderConfig);
        st->rangeCoderConfig = NULL;
    }

	// destroy codec st
    free(st);
    *codecSt = NULL;

	return 0;
}

void DecryterCube(char *ptxData, int len)
{
	int i;
	for (i = 0; i < len; i++)
		ptxData[i] = ptxData[i] ^ 0x55;
}

int16_t InitNeuralCodec(
	modul_structure *fModel,
    NeuralCodecHandle *baseCodecSt,
    NeuralCodecHandle *contextCodecSt,
    TypeModel modelType
)
{
    // Init codec according to model type
    if (modelType == VAE) {
        // init base model
        if (LoadModel(fModel, baseCodecSt, 0, BLOCK_LEN_LONG) != 0) {
            *contextCodecSt = NULL;
            return -1;
        }
        // no context model
        *contextCodecSt = NULL;
    }
    else {
        // init base model
        if (LoadModel(fModel, baseCodecSt, 1, BLOCK_LEN_LONG) != 0) {
            *contextCodecSt = NULL;
            return -1;
        }
        // init context model
        int16_t numFeatEncode = (*baseCodecSt)->numLatentEncode;
        if (LoadModel(fModel, contextCodecSt, 0, numFeatEncode) != 0) {
            DestroyModel(baseCodecSt);
            *contextCodecSt = NULL;
            return -1;
        }
    }

    return 0;
}


int16_t DestroyNeuralCodec(
    NeuralCodecHandle *baseCodecSt,
    NeuralCodecHandle *contextCodecSt
)
{
	// deinit base codec
    DestroyModel(baseCodecSt);

    // deinit context codec
    if ((*contextCodecSt) != NULL) {
        DestroyModel(contextCodecSt);
    }

	return 0;
}


int16_t InitNeuralQcData(
    NeuralQcData *neuralQcData
)
{
    SetUC(neuralQcData->baseBitstream, 0, MAX_QC_BS_LENGTH);
    neuralQcData->baseNumBytes = 0;

    SetUC(neuralQcData->contextBitstream, 0, MAX_QC_BS_LENGTH);
    neuralQcData->contextNumBytes = 0;

    neuralQcData->featureScale = 1.0f;
    neuralQcData->isFeatAmplified = 0;
    neuralQcData->scaleQIdx = -1;

    SetFloat(neuralQcData->nfParam, 0.0f, N_GROUP_SHORT_WIN);
    SetShort(neuralQcData->nfParamQIdx, -1, N_GROUP_SHORT_WIN);

    return 0;
}
