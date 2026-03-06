#pragma once

#include <string>
#include <vector>
#include <CubismFramework.hpp>
#include <Model/CubismUserModel.hpp>
#include <Rendering/OpenGL/CubismRenderer_OpenGLES2.hpp>

struct HitArea {
    Csm::CubismIdHandle Id;
    Csm::csmString Name;
};

class LAppModel : public Csm::CubismUserModel
{
public:
    LAppModel();
    virtual ~LAppModel();

    void LoadAssets(const std::string& dir, const std::string& fileName);
    void Update();
    void Draw(Csm::CubismMatrix44& matrix);
    
    // Hit Test: x, y are Device Coordinates (-1.0 to 1.0)
    bool HitTest(float x, float y);
    
    void SetViewMatrix(const Csm::CubismMatrix44& m) { _viewMatrix = m; }
    
    Csm::csmFloat32 GetCanvasWidth() const { return _model ? _model->GetCanvasWidth() : 0.0f; }
    Csm::csmFloat32 GetCanvasHeight() const { return _model ? _model->GetCanvasHeight() : 0.0f; }

private:
    Live2D::Cubism::Framework::Rendering::CubismRenderer_OpenGLES2* GetRenderer() { 
        return dynamic_cast<Live2D::Cubism::Framework::Rendering::CubismRenderer_OpenGLES2*>(CubismUserModel::GetRenderer<Live2D::Cubism::Framework::Rendering::CubismRenderer_OpenGLES2>()); 
    }
    
    // Helper to load file content
    static std::vector<Csm::csmByte> LoadFileAsBytes(const std::string& filePath);
    
    // Helper to load texture
    void LoadTexture(Csm::csmInt32 modelTextureIndex, const std::string& texturePath);

    std::string _modelHomeDir;
    Csm::csmFloat32 _userTimeSeconds;
    
    Csm::csmVector<HitArea> _hitAreas;
    
    // Keep moc data alive
    std::vector<Csm::csmByte> _mocBuffer;
    
    Csm::CubismMatrix44 _viewMatrix;
    
    Csm::csmVector<Csm::CubismIdHandle> _userAreaIds;
};
