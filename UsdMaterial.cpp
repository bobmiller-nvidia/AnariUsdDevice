// Copyright 2020 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "UsdMaterial.h"
#include "UsdBridge/UsdBridge.h"
#include "UsdAnari.h"
#include "UsdDevice.h"
#include "UsdSampler.h"
#include "UsdDataArray.h"

#define SamplerType ANARI_SAMPLER
using SamplerUsdType = AnariToUsdBridgedObject<SamplerType>::Type;

DEFINE_PARAMETER_MAP(UsdMaterial, 
  REGISTER_PARAMETER_MACRO("name", ANARI_STRING, name)
  REGISTER_PARAMETER_MACRO("usd::name", ANARI_STRING, usdName)
  REGISTER_PARAMETER_MACRO("usd::time", ANARI_FLOAT64, timeStep)
  REGISTER_PARAMETER_MACRO("usd::timeVarying", ANARI_INT32, timeVarying)
  REGISTER_PARAMETER_MACRO("usd::time::color", ANARI_FLOAT64, colorSamplerTimeStep)
  REGISTER_PARAMETER_MACRO("usd::time::opacity", ANARI_FLOAT64, opacitySamplerTimeStep)
  REGISTER_PARAMETER_MACRO("usd::time::emissiveColor", ANARI_FLOAT64, emissiveSamplerTimeStep)
  REGISTER_PARAMETER_MACRO("usd::time::emissiveIntensity", ANARI_FLOAT64, emissiveIntensitySamplerTimeStep)
  REGISTER_PARAMETER_MACRO("usd::time::roughness", ANARI_FLOAT64, roughnessSamplerTimeStep)
  REGISTER_PARAMETER_MACRO("usd::time::metallic", ANARI_FLOAT64, metallicSamplerTimeStep)
  REGISTER_PARAMETER_MACRO("usd::time::ior", ANARI_FLOAT64, iorSamplerTimeStep)
  REGISTER_PARAMETER_MULTITYPE_MACRO("color", ANARI_FLOAT32_VEC3, SamplerType, ANARI_STRING, color)
  REGISTER_PARAMETER_MULTITYPE_MACRO("opacity", ANARI_FLOAT32, SamplerType, ANARI_STRING, opacity)
  REGISTER_PARAMETER_MULTITYPE_MACRO("emissiveColor", ANARI_FLOAT32_VEC3, SamplerType, ANARI_STRING, emissiveColor)
  REGISTER_PARAMETER_MULTITYPE_MACRO("emissiveIntensity", ANARI_FLOAT32, SamplerType, ANARI_STRING, emissiveIntensity)
  REGISTER_PARAMETER_MULTITYPE_MACRO("roughness", ANARI_FLOAT32, SamplerType, ANARI_STRING, roughness)
  REGISTER_PARAMETER_MULTITYPE_MACRO("metallic", ANARI_FLOAT32, SamplerType, ANARI_STRING, metallic)
  REGISTER_PARAMETER_MULTITYPE_MACRO("ior", ANARI_FLOAT32, SamplerType, ANARI_STRING, ior) 
)

using DMI = UsdMaterial::MaterialDMI;

UsdMaterial::UsdMaterial(const char* name, const char* type, UsdBridge* bridge, UsdDevice* device)
  : BridgedBaseObjectType(ANARI_MATERIAL, name, bridge)
{
  if (!std::strcmp(type, "matte"))
  {
    isPbr = false;
    isTranslucent = false;
  }
  else if (!std::strcmp(type, "transparentMatte"))
  {
    isPbr = false;
    isTranslucent = true;
  }
  if (!std::strcmp(type, "pbr"))
  {
    isPbr = true;
    isTranslucent = false;
  }
  else if (!std::strcmp(type, "transparentPbr"))
  {
    isPbr = true;
    isTranslucent = true;
  }
  else
  {
    device->reportStatus(this, ANARI_MATERIAL, ANARI_SEVERITY_ERROR, ANARI_STATUS_INVALID_ARGUMENT, "UsdMaterial '%s' intialization error: unknown material type", getName());
  }
}

UsdMaterial::~UsdMaterial()
{
#ifdef OBJECT_LIFETIME_EQUALS_USD_LIFETIME
  if(usdBridge)
    usdBridge->DeleteMaterial(usdHandle);
#endif
}

void UsdMaterial::filterSetParam(const char *name,
  ANARIDataType type,
  const void *mem,
  UsdDevice* device)
{
  if (filterNameParam(name, type, mem, device))
    setParam(name, type, mem, device);
}

void UsdMaterial::filterResetParam(const char *name)
{
  resetParam(name);
}

template<typename ValueType>
bool UsdMaterial::getMaterialInputSourceName(UsdMaterialMultiTypeParameter<ValueType> param, MaterialDMI dataMemberId, UsdDevice* device, const UsdLogInfo& logInfo)
{
  UsdSharedString* anariAttribStr = nullptr;
  param.Get(anariAttribStr);
  const char* anariAttrib = UsdSharedString::c_str(anariAttribStr);

  if(anariAttrib && !strcmp(anariAttrib, "objectPosition"))
  {
    // In case of a per-instance specific attribute name, there can be only one change of the attribute name.
    // Otherwise there is a risk of the material 
    if(instanceAttributeAttached)
    {
      reportStatusThroughDevice(logInfo, ANARI_SEVERITY_WARNING, ANARI_STATUS_INVALID_ARGUMENT,
        "UsdMaterial '%s' binds one of its parameters to %s, but is transitively bound to both an instanced geometry (cones, spheres, cylinders) and regular geometry. \
        This is incompatible with USD, which demands a differently bound name for those categories. \
        Please create two different samplers and bind each to only one of both categories of geometry. \
        The parameter value will be updated, but may therefore invalidate previous bindings to the objectPosition attribute.", logInfo.sourceName, "'objectPosition'");
    }

    const char* usdAttribName = AnariAttributeToUsdName(anariAttrib, perInstance, logInfo);

    materialInputNames.push_back(UsdBridge::MaterialInputSourceName(usdAttribName, dataMemberId));

    return true;
  }

  return false;
}

template<typename ValueType>
bool UsdMaterial::getSamplerRefData(UsdMaterialMultiTypeParameter<ValueType> param, double refTimeStep, MaterialDMI dataMemberId, UsdDevice* device, const UsdLogInfo& logInfo)
{
  UsdSampler* sampler = nullptr;
  param.Get(sampler);

  if(sampler)
  {
    const UsdSamplerData& samplerParamData = sampler->getReadParams();

    double worldTimeStep = device->getReadParams().timeStep;
    double samplerObjTimeStep = samplerParamData.timeStep;
    double samplerRefTime = selectRefTime(refTimeStep, samplerObjTimeStep, worldTimeStep);
    
    // Reading child (sampler) data in the material has the consequence that the sampler's parameters as they were last committed are in effect "part of" the material parameter set, at this point of commit. 
    // So in case a sampler at a particular timestep is referenced from a material at two different world timesteps 
    // - ie. for this world timestep, a particular timestep of an image already committed and subsequently referenced at some other previous world timestep is reused - 
    // the user needs to make sure that not only the timestep is set correctly on the sampler for the commit (which is by itself lightweight, as it does not trigger a full commit), 
    // but also that the parameters read here have been re-set on the sampler to the values belonging to the referenced timestep, as if there is no USD representation of a sampler object. 
    // Setting those parameters will in turn trigger a full commit of the sampler object, which is in theory inefficient.
    // However, in case of a sampler this is not a problem in practice; data transfer is only a concern when the filename is *not* set, at which point a relative file corresponding
    // to the sampler timestep will be automatically chosen and set for the material, without the sampler object requiring any updates. 
    // In case a filename *is* set, only the filename is used and no data transfer/file io operations are performed.
    const char* imageUrl = UsdSharedString::c_str(samplerParamData.imageUrl);
    bool fNameTimeVarying = (samplerParamData.timeVarying & 1);
    
    const char* imageName = nullptr;
    int imageNumComponents = 4;
    if(samplerParamData.imageData)
    {
      imageName = UsdSharedString::c_str(samplerParamData.imageData->getName());
      imageNumComponents = static_cast<int>(anari::componentsOf(samplerParamData.imageData->getType()));
    }
    UsdSamplerRefData samplerRefData = {imageNumComponents, samplerRefTime, dataMemberId};

    samplerHandles.push_back(sampler->getUsdHandle());
    samplerRefDatas.push_back(samplerRefData);
  }

  return false;
}

template<typename ValueType>
void UsdMaterial::assignParameterToMaterialInput(UsdMaterialMultiTypeParameter<ValueType> param, 
  UsdBridgeMaterialData::MaterialInput<ValueType> matInput, const UsdLogInfo& logInfo)
{
  param.Get(matInput.Value);

  UsdSharedString* anariAttribStr = nullptr;
  matInput.SrcAttrib = param.Get(anariAttribStr) ? 
    AnariAttributeToUsdName(anariAttribStr->c_str(), perInstance, logInfo) : 
    nullptr; 

  UsdSampler* sampler = nullptr;
  matInput.Sampler = param.Get(sampler);
}

void UsdMaterial::setPerInstance(bool enable, UsdDevice* device)
{ 
  const UsdMaterialData& paramData = getReadParams();
  
  if(perInstance != enable)
  {
    UsdLogInfo logInfo = {device, this, ANARI_MATERIAL, getName()};

    double worldTimeStep = device->getReadParams().timeStep;
    double dataTimeStep = selectObjTime(paramData.timeStep, worldTimeStep);

    // Fix up any parameters that have a geometry-type-dependent name set as source attribute
    materialInputNames.clear();

    bool hasPositionAttrib = 
      getMaterialInputSourceName(paramData.color, DMI::DIFFUSE, device, logInfo) ||
      getMaterialInputSourceName(paramData.opacity, DMI::OPACITY, device, logInfo) ||
      getMaterialInputSourceName(paramData.emissiveColor, DMI::EMISSIVECOLOR, device, logInfo) ||
      getMaterialInputSourceName(paramData.emissiveIntensity, DMI::EMISSIVEINTENSITY, device, logInfo) ||
      getMaterialInputSourceName(paramData.roughness, DMI::ROUGHNESS, device, logInfo) ||
      getMaterialInputSourceName(paramData.metallic, DMI::METALLIC, device, logInfo) ||
      getMaterialInputSourceName(paramData.ior, DMI::IOR, device, logInfo);

    if(materialInputNames.size())
      usdBridge->ChangeMaterialInputSourceNames(usdHandle, materialInputNames.data(), materialInputNames.size(), dataTimeStep, (DMI)paramData.timeVarying);
      
    if(hasPositionAttrib)
      instanceAttributeAttached = true; // As soon as any parameter is set to a position attribute, the geometry type for this material is 'locked-in'

    perInstance = enable; 
  }

  if(paramData.color.type == SamplerType)
  {
    UsdSampler* colorSampler = nullptr;
    if (paramData.color.Get(colorSampler))
    {
      colorSampler->setPerInstance(enable, device);
    }
  }
}

bool UsdMaterial::deferCommit(UsdDevice* device)
{
  //const UsdMaterialData& paramData = getReadParams();

  //if(UsdObjectNotInitialized<SamplerUsdType>(paramData.color.type == SamplerType))
  //{
  //  return true;
  //}
  return false;
}

bool UsdMaterial::doCommitData(UsdDevice* device)
{
  if(!usdBridge || !device->getReadParams().outputMaterial)
    return false;

  bool isNew = false;
  if (!usdHandle.value)
    isNew = usdBridge->CreateMaterial(getName(), usdHandle);

  if (paramChanged || isNew)
  {
    const UsdMaterialData& paramData = getReadParams();

    double worldTimeStep = device->getReadParams().timeStep;
    double dataTimeStep = selectObjTime(paramData.timeStep, worldTimeStep);

    UsdBridgeMaterialData matData;
    matData.HasTranslucency = isTranslucent;
    matData.IsPbr = isPbr;

    UsdLogInfo logInfo = {device, this, ANARI_MATERIAL, getName()};
    
    assignParameterToMaterialInput(paramData.color, matData.Diffuse, logInfo);
    assignParameterToMaterialInput(paramData.opacity, matData.Opacity, logInfo);
    assignParameterToMaterialInput(paramData.emissiveColor, matData.Emissive, logInfo);
    assignParameterToMaterialInput(paramData.emissiveIntensity, matData.EmissiveIntensity, logInfo);
    assignParameterToMaterialInput(paramData.roughness, matData.Roughness, logInfo);
    assignParameterToMaterialInput(paramData.metallic, matData.Metallic, logInfo);
    assignParameterToMaterialInput(paramData.ior, matData.Ior, logInfo);

    matData.TimeVarying = (DMI) paramData.timeVarying;

    usdBridge->SetMaterialData(usdHandle, matData, dataTimeStep);

    paramChanged = false;

    return paramData.color.type == SamplerType; // Only commit refs when material actually contains a texture (filename param from diffusemap is required)
  }

  return false;
}

void UsdMaterial::doCommitRefs(UsdDevice* device)
{
  const UsdMaterialData& paramData = getReadParams();

  double worldTimeStep = device->getReadParams().timeStep;

  samplerHandles.clear();
  samplerRefDatas.clear();

  UsdLogInfo logInfo = {device, this, ANARI_MATERIAL, getName()};

  getSamplerRefData(paramData.color, paramData.colorSamplerTimeStep, DMI::DIFFUSE, device, logInfo);
  getSamplerRefData(paramData.opacity, paramData.opacitySamplerTimeStep, DMI::OPACITY, device, logInfo);
  getSamplerRefData(paramData.emissiveColor, paramData.emissiveSamplerTimeStep, DMI::EMISSIVECOLOR, device, logInfo);
  getSamplerRefData(paramData.emissiveIntensity, paramData.emissiveIntensitySamplerTimeStep, DMI::EMISSIVEINTENSITY, device, logInfo);
  getSamplerRefData(paramData.roughness, paramData.roughnessSamplerTimeStep, DMI::ROUGHNESS, device, logInfo);
  getSamplerRefData(paramData.metallic, paramData.metallicSamplerTimeStep, DMI::METALLIC, device, logInfo);
  getSamplerRefData(paramData.ior, paramData.iorSamplerTimeStep, DMI::IOR, device, logInfo);

  if(samplerHandles.size())
    usdBridge->SetSamplerRefs(usdHandle, samplerHandles.data(), samplerRefDatas.data(), samplerHandles.size(), worldTimeStep);
  else
    usdBridge->DeleteSamplerRefs(usdHandle, worldTimeStep);
}