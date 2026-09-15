#include "form.h"
#include "animation.h"
namespace breff::codec {
Json effectForm(const Json& model) {
    Json root={{"kind","root"},{"children",Json::array()}};
    const auto emitter=model.value("emitter",Json::object());
    root["children"].push_back(recordForm("EmitterData",emitter,"/emitter","emitter"));
    auto particle=recordForm("ParticleData",model.value("particle",Json::object()),"/particle","particle");
    Json offsets={{"kind","record"},{"path","/particle/rotationOffsets"},{"label","Particle rotation offsets"},{"children",Json::array()}};
    const char* slots[]={"texture1","texture2","textureInd"};const char* axes[]={"X","Y","Z"};
    for(unsigned i=0;i<3;++i) {
        const std::string path=std::string("/particle/")+slots[i];
        offsets["children"].push_back(scalarForm(path+"/rotationOffset",std::string(axes[i])+" offset (radians)",0.f));
        offsets["children"].push_back(scalarForm(path+"/rotationOffsetRandom",std::string(axes[i])+" randomness (%)",0));
        particle["children"].push_back({{"kind","texture"},{"path",path},{"label",slots[i]},{"default",Json::object()}});
    }
    particle["children"].push_back(offsets);root["children"].push_back(particle);
    Json animations={{"kind","list"},{"path","/animations"},{"label","animations"},{"fixedLength",nullptr},{"maxLength",65535},
        {"default",defaultAnimation("ParticleSize",emitter)},{"children",Json::array()}};
    const auto tracks=model.value("animations",Json::array());
    for(size_t i=0;i<tracks.size();++i)animations["children"].push_back(animationForm(tracks[i],emitter,"/animations/"+std::to_string(i),"["+std::to_string(i)+"]"));
    root["children"].push_back(animations);return root;
}
}
