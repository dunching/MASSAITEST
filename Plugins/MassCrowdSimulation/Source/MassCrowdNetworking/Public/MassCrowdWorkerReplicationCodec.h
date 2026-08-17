#pragma once

#include "CoreMinimal.h"
#include "MassCrowdWorkerNetworkState.h"

class MASSCROWDNETWORKING_API FCrowdWorkerReplicationCodec
{
public:
  static bool EncodeCheckpoint(
    const FCrowdWorkerNetworkCheckpoint& Checkpoint,
    const FCrowdWorkerNetworkStateConfig& Config,
    TArray<uint8>& OutBytes);
  static bool DecodeCheckpoint(
    TConstArrayView<uint8> Bytes,
    const FCrowdWorkerNetworkStateConfig& Config,
    FCrowdWorkerNetworkCheckpoint& OutCheckpoint);

  static bool EncodeIntent(
    const FCrowdWorkerIntentBatch& Batch,
    const FCrowdWorkerNetworkStateConfig& Config,
    TArray<uint8>& OutBytes);
  static bool DecodeIntent(
    TConstArrayView<uint8> Bytes,
    const FCrowdWorkerNetworkStateConfig& Config,
    FCrowdWorkerIntentBatch& OutBatch);
  static bool EncodeCorrection(
    const FCrowdWorkerAuthorityCorrectionBatch& Correction,
    const FCrowdWorkerNetworkStateConfig& Config,
    TArray<uint8>& OutBytes);
  static bool DecodeCorrection(
    TConstArrayView<uint8> Bytes,
    const FCrowdWorkerNetworkStateConfig& Config,
    FCrowdWorkerAuthorityCorrectionBatch& OutCorrection);

};
