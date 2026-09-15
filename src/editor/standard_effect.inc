R"STANDARD({
  "emitter": {
    "commonFlags": {
      "syncChildrenLifetime": false,
      "invisible": false,
      "infiniteLifetime": false,
      "inheritParticleScale": true,
      "inheritParticleRotate": false,
      "inheritChildEmitterScale": false,
      "inheritChildEmitterRotate": false,
      "disableEmitterCalculations": false,
      "inheritParticlePivot": false,
      "inheritChildPivot": false,
      "inheritChildParticleScale": false,
      "inheritChildParticleRotate": false,
      "relocateComplete": false
    },
    "typeSpecificFlags": {},
    "emitFlags": {
      "lodEnabled": false,
      "unk4": false,
      "billboardY": false,
      "billboard": false,
      "fixedInterval": false,
      "fixedPosition": false,
      "instance": false
    },
    "shape": "Point",
    "emitLifetime": 60,
    "particleLifetime": 60,
    "particleLifetimeRandomness": 0,
    "inheritChildParticleTranslate": 0,
    "emissionIntervalRandomness": 0,
    "emissionVolumeRandomness": 0,
    "emissionVolume": 1.0,
    "emissionStartTime": 0,
    "emissionPast": 0,
    "emissionInterval": 0,
    "inheritParticleTranslate": 0,
    "inheritChildEmitTranslate": 0,
    "shapeParams": 0,
    "shapeDivisions": 0,
    "initialVelocityRandomness": 0,
    "initialMomentumRandomness": 0,
    "speed": 0.0,
    "yDiffusionSpeed": 0.0,
    "randomDirSpeed": 0.0,
    "normalDirSpeed": 0.0,
    "normalDirDiffusionAngle": 0.0,
    "specifiedDirEmissionSpeed": 0.0,
    "specifiedDirDiffusionAngle": 0.0,
    "specifiedDir": {
      "x": 0.0,
      "y": 1.5707963705062866,
      "z": 0.0
    },
    "scale": {
      "x": 1.0,
      "y": 1.0,
      "z": 1.0
    },
    "rotation": {
      "x": 0.0,
      "y": 0.0,
      "z": 0.0
    },
    "translation": {
      "x": 0.0,
      "y": 0.0,
      "z": 0.0
    },
    "nearLodPlane": 0,
    "farLodPlane": 0,
    "minLodEmitRate": 0,
    "lodAlpha": 0,
    "randomSeed": 0,
    "userData": 0,
    "drawFlags": {
      "zCompareEnabled": true,
      "zUpdateEnabled": false,
      "zCompareBeforeTex": true,
      "clippingDisabled": false,
      "useTexture1": false,
      "useTexture2": false,
      "useIndirectTexture": false,
      "isTexture1Projection": false,
      "isTexture2Projection": false,
      "isIndirectTextureProjection": false,
      "invisible": false,
      "reverseDrawOrder": false,
      "fogEnabled": false,
      "xYLinkSize": true,
      "xYLinkScale": true
    },
    "alphaCompare1": "Always",
    "alphaCompare2": "Always",
    "alphaCompareOperator": "And",
    "flagClamp": true,
    "indirectTargetStages": 0,
    "tevStages": [
      {
        "texture": 0,
        "colorSelectionA": "Zero",
        "colorSelectionB": "Zero",
        "colorSelectionC": "Zero",
        "colorSelectionD": "Color0",
        "colorOperation": "Add",
        "colorBias": "Zero",
        "colorScale": "MultiplyBy1",
        "colorClamp": true,
        "colorRegister": "OutputColor",
        "alphaSelectionA": "Zero",
        "alphaSelectionB": "Zero",
        "alphaSelectionC": "Zero",
        "alphaSelectionD": "Alpha0",
        "alphaOperation": "Add",
        "alphaBias": "Zero",
        "alphaScale": "MultiplyBy1",
        "alphaClamp": true,
        "alphaRegister": "OutputAlpha",
        "constantColorSelection": "Constant1_1",
        "constantAlphaSelection": "Constant1_1"
      }
    ],
    "blendType": "Blend",
    "blendSrcFactor": "SourceAlpha",
    "blendDstFactor": "InverseSourceAlpha",
    "blendOperation": "Clear",
    "colorInput": {
      "rasColor": "Null",
      "tevColor1": "Color1Primary",
      "tevColor2": "Color1Primary",
      "tevColor3": "Null",
      "tevKColor1": "Null",
      "tevKColor2": "Null",
      "tevKColor3": "Null",
      "tevKColor4": "Null"
    },
    "alphaInput": {
      "rasColor": "Null",
      "tevColor1": "Color1Primary",
      "tevColor2": "Null",
      "tevColor3": "Null",
      "tevKColor1": "Null",
      "tevKColor2": "Null",
      "tevKColor3": "Null",
      "tevKColor4": "Null"
    },
    "zCompareFunc": "LessOrEqual",
    "alphaSwing": {
      "type": "NoSwing",
      "cyclePeriod": 0,
      "randomness": 0,
      "amplitude": 0
    },
    "lighting": {
      "mode": "Off",
      "type": "NoLighting",
      "ambientColor": {
        "r": 0,
        "g": 0,
        "b": 0,
        "a": 255
      },
      "diffuseColor": {
        "r": 255,
        "g": 255,
        "b": 255,
        "a": 255
      },
      "radius": 50.0,
      "position": {
        "x": 0.0,
        "y": 10.0,
        "z": 0.0
      }
    },
    "indirectTextureMatrix": [
      0.0,
      -0.0,
      0.0,
      0.0,
      0.0,
      0.0
    ],
    "indirectTextureScale": -17,
    "pivotX": 0,
    "pivotY": 0,
    "particleType": "Billboard",
    "particleOptions": {
      "expression": "Normal",
      "yDirection": "Speed",
      "rotationalAxis": "XOnly",
      "speedBasedVertical": false
    },
    "zOffset": 0.0
  },
  "particle": {
    "color1Primary": {
      "r": 255,
      "g": 255,
      "b": 255,
      "a": 255
    },
    "color1Secondary": {
      "r": 138,
      "g": 0,
      "b": 0,
      "a": 0
    },
    "color2Primary": {
      "r": 255,
      "g": 255,
      "b": 255,
      "a": 255
    },
    "color2Secondary": {
      "r": 0,
      "g": 0,
      "b": 0,
      "a": 0
    },
    "particleSize": {
      "x": 5.0,
      "y": 5.0
    },
    "particleScale": {
      "x": 1.0,
      "y": 1.0
    },
    "particleRotation": {
      "x": 0.0,
      "y": 0.0,
      "z": 0.0
    },
    "alphaCompareValue0": 0,
    "alphaCompareValue1": 0,
    "texture1": {
      "name": "",
      "scale": {
        "x": 1.0,
        "y": 1.0
      },
      "rotation": 0.0,
      "translation": {
        "x": 0.0,
        "y": 0.0
      },
      "wrapS": "Clamp",
      "wrapT": "Clamp",
      "reverseMode": "NoReverse",
      "rotationOffsetRandom": 0,
      "rotationOffset": 0.0
    },
    "texture2": {},
    "textureInd": {}
  },
  "animations": []
}
)STANDARD"