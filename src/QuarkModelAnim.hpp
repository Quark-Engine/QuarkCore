#ifndef __QUARK_MODEL_ANIM_H__
#define __QUARK_MODEL_ANIM_H__

struct aiScene;

namespace qc {

struct Model;

void qcPopulateModelSkeleton(const aiScene* scene, Model& model);

void qcFreeModelSkeleton(Model& model);

} // namespace qc

#endif // __QUARK_MODEL_ANIM_H__