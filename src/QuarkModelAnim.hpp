#ifndef __QUARK_MODEL_ANIM_H__
#define __QUARK_MODEL_ANIM_H__

#include <assimp/matrix4x4.h>

struct aiScene;
struct aiBone;
struct Model;

void qcPopulateModelSkeleton(const aiScene* pScene, ::Model& model);

int qcFindSkeletonBoneIndex(const aiScene* pScene, const ::Model& model, const aiBone* pBone);

void qcSetMeshBoneOffset(::Mesh& mesh, unsigned int boneIndex, unsigned int boneCount,
                         const aiMatrix4x4& offset);

void qcFreeModelSkeleton(::Model& model);

#endif // __QUARK_MODEL_ANIM_H__
