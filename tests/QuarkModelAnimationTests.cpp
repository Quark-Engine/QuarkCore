#include <QuarkCore/QuarkCore.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace qc;

namespace {

bool NearlyEqual(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}

} // namespace

int main() {
    Model model{};
    model.meshCount = 1;
    model.meshes = new Mesh[1]{};
    Mesh& mesh = model.meshes[0];
    mesh.vertexCount = 1;
    mesh.triangleCount = 1;
    mesh.vertices = new float[3]{0.0f, 0.0f, 0.0f};
    mesh.normals = new float[3]{0.0f, 0.0f, 1.0f};
    mesh.texcoords = new float[2]{0.0f, 0.0f};
    mesh.indices = new unsigned short[3]{0, 0, 0};
    mesh.boneCount = 1;
    mesh.boneIndices = new unsigned char[4]{0, 0, 0, 0};
    mesh.boneWeights = new float[4]{1.0f, 0.0f, 0.0f, 0.0f};

    model.skeleton.boneCount = 1;
    model.skeleton.bones = new BoneInfo[1];
    model.skeleton.bones[0].parent = -1;
    std::strncpy(model.skeleton.bones[0].name, "BoneRoot", sizeof(model.skeleton.bones[0].name) - 1);
    model.skeleton.bindPose = new Transform[1];
    model.skeleton.bindPose[0] = Transform{{0.0f, 0.0f, 0.0f}, Quaternion{0.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}};
    model.boneMatrices = new Matrix[1];

    ModelAnimation animation{};
    animation.boneCount = 1;
    animation.keyframeCount = 1;
    animation.keyframePoses = new ModelAnimPose[1];
    animation.keyframePoses[0] = new Transform[1];
    animation.keyframePoses[0][0] = Transform{{2.0f, 0.0f, 0.0f}, Quaternion{0.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}};

    UpdateModelAnimation(model, animation, 0.0f);

    if (model.boneMatrices == nullptr) {
        std::cerr << "boneMatrices were not allocated" << std::endl;
        return 1;
    }

    if (mesh.animVertices == nullptr || mesh.animNormals == nullptr) {
        std::cerr << "animated vertex buffers were not generated" << std::endl;
        return 2;
    }

    if (!NearlyEqual(mesh.animVertices[0], 2.0f)) {
        std::cerr << "mesh vertex was not skinned to the animation transform: "
                  << mesh.animVertices[0] << std::endl;
        return 3;
    }

    if (!NearlyEqual(model.boneMatrices[0].m[12], 2.0f)) {
        std::cerr << "bone matrix translation is wrong: " << model.boneMatrices[0].m[12] << std::endl;
        return 4;
    }

    delete[] model.meshes;
    delete[] model.skeleton.bones;
    delete[] model.skeleton.bindPose;
    delete[] model.boneMatrices;
    for (int i = 0; i < animation.keyframeCount; ++i) {
        delete[] animation.keyframePoses[i];
    }
    delete[] animation.keyframePoses;

    return 0;
}
