// Copyright 2020 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "UsdBridgedBaseObject.h"
#include "UsdBridgeNumerics.h"
#include "UsdDeviceUtils.h"
#include <limits>

class UsdSampler;

template<typename ValueType>
using UsdMaterialMultiTypeParameter = UsdMultiTypeParameter<ValueType, UsdSampler*, UsdSharedString*>;

struct UsdMaterialData
{
  UsdSharedString* name = nullptr;
  UsdSharedString* usdName = nullptr;

  double timeStep = std::numeric_limits<float>::quiet_NaN();
  int timeVarying = 0; // Bitmask indicating which attributes are time-varying. 0: color, 1: opacity, 2: specular, 
    // 3: emissive, 4: emissiveintensity, 5: roughness, 6: metallic, 7: ior

  // Standard parameters
  UsdMaterialMultiTypeParameter<UsdFloat3> color = {{ 1.0f, 1.0f, 1.0f }, ANARI_FLOAT32_VEC3};
  UsdMaterialMultiTypeParameter<float> opacity = {0.0f, ANARI_FLOAT32};

  // Possible PBR parameters
  UsdMaterialMultiTypeParameter<UsdFloat3> specular = {{ 1.0f, 1.0f, 1.0f }, ANARI_FLOAT32_VEC3};
  UsdMaterialMultiTypeParameter<UsdFloat3> emissive = {{ 1.0f, 1.0f, 1.0f }, ANARI_FLOAT32_VEC3};
  UsdMaterialMultiTypeParameter<float> emissiveIntensity = {0.0f, ANARI_FLOAT32};
  UsdMaterialMultiTypeParameter<float> shininess = {0.5f, ANARI_FLOAT32};
  UsdMaterialMultiTypeParameter<float> metallic = {-1.0f, ANARI_FLOAT32};
  UsdMaterialMultiTypeParameter<float> ior = {1.0f, ANARI_FLOAT32};

  double colorSamplerTimeStep = std::numeric_limits<float>::quiet_NaN();
  double opacitySamplerTimeStep = std::numeric_limits<float>::quiet_NaN();
  double specularSamplerTimeStep = std::numeric_limits<float>::quiet_NaN();
  double emissiveSamplerTimeStep = std::numeric_limits<float>::quiet_NaN();
  double emissiveIntensitySamplerTimeStep = std::numeric_limits<float>::quiet_NaN();
  double shininessSamplerTimeStep = std::numeric_limits<float>::quiet_NaN();
  double metallicSamplerTimeStep = std::numeric_limits<float>::quiet_NaN();
  double iorSamplerTimeStep = std::numeric_limits<float>::quiet_NaN();
};

class UsdMaterial : public UsdBridgedBaseObject<UsdMaterial, UsdMaterialData, UsdMaterialHandle>
{
  public:
    using MaterialDMI = UsdBridgeMaterialData::DataMemberId;

    UsdMaterial(const char* name, const char* type, UsdBridge* bridge, UsdDevice* device);
    ~UsdMaterial();

    void filterSetParam(const char *name,
      ANARIDataType type,
      const void *mem,
      UsdDevice* device) override;

    void filterResetParam(
      const char *name) override;

    bool isPerInstance() const { return perInstance; }
    void setPerInstance(bool enable, UsdDevice* device);

  protected:
    using MaterialInputSourceNamePair = std::pair<const char*, MaterialDMI>;

    template<typename ValueType>
    bool getMaterialInputSourceName(UsdMaterialMultiTypeParameter<ValueType> param,
      MaterialDMI dataMemberId, UsdDevice* device, const UsdLogInfo& logInfo);

    template<typename ValueType>
    bool getSamplerRefData(UsdMaterialMultiTypeParameter<ValueType> param, double refTimeStep, 
      MaterialDMI dataMemberId, UsdDevice* device, const UsdLogInfo& logInfo);

    template<typename ValueType>
    void assignParameterToMaterialInput(
      UsdMaterialMultiTypeParameter<ValueType> param, 
      UsdBridgeMaterialData::MaterialInput<ValueType> matInput, 
      const UsdLogInfo& logInfo);

    bool deferCommit(UsdDevice* device) override;
    bool doCommitData(UsdDevice* device) override;
    void doCommitRefs(UsdDevice* device) override;

    bool isTranslucent = false;
    bool isPbr = false;

    bool perInstance = false; // Whether material is attached to a point instancer
    bool instanceAttributeAttached = false; // Whether a value to any parameter has been set which in USD is different between per-instance and regular geometries

    OptionalList<MaterialInputSourceNamePair> materialInputNames;
    OptionalList<UsdSamplerHandle> samplerHandles;
    OptionalList<UsdSamplerRefData> samplerRefDatas;
};