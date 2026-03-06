#include "LAppModel.h"
#include <fstream>
#include <iostream>
#include <vector>
#include <direct.h> // for _getcwd

// STB Image for texture loading
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <CubismModelSettingJson.hpp>
#include <Motion/CubismMotion.hpp>
#include <Physics/CubismPhysics.hpp>
#include <CubismDefaultParameterId.hpp>
#include <Rendering/OpenGL/CubismRenderer_OpenGLES2.hpp>
#include <Utils/CubismString.hpp>
#include <Id/CubismIdManager.hpp>

using namespace Live2D::Cubism::Framework;
using namespace Live2D::Cubism::Framework::DefaultParameterId;

LAppModel::LAppModel()
    : CubismUserModel()
    , _userTimeSeconds(0.0f)
{ }

LAppModel::~LAppModel()
{
    // Release textures
    // Use GetRenderer() helper which uses the public API
    auto renderer = GetRenderer();
    if (renderer)
    {
        const auto& textures = renderer->GetBindedTextures();
        for (Csm::csmMap<Csm::csmInt32, GLuint>::const_iterator i = textures.Begin(); i != textures.End(); ++i)
        {
            GLuint textureId = i->Second;
            glDeleteTextures(1, &textureId);
        }
    }
}

std::vector<Csm::csmByte> LAppModel::LoadFileAsBytes(const std::string& filePath)
{
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "[LAppModel] Failed to open file: " << filePath << std::endl;
        // 打印当前工作目录
        char cwd[1024];
        if (_getcwd(cwd, sizeof(cwd)) != NULL) {
            std::cerr << "[LAppModel] Current working directory: " << cwd << std::endl;
        }
        return {};
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<Csm::csmByte> buffer(size);
    if (file.read(reinterpret_cast<char*>(buffer.data()), size))
    {
        return buffer;
    }
    return {};
}

void LAppModel::LoadAssets(const std::string& dir, const std::string& fileName)
{
    _modelHomeDir = dir;

    // 1. Load Model Setting (model3.json)
    std::string settingPath = dir + "/" + fileName;
    std::vector<Csm::csmByte> settingBuffer = LoadFileAsBytes(settingPath);
    
    if (settingBuffer.empty()) return;

    CubismModelSettingJson modelSetting(settingBuffer.data(), static_cast<Csm::csmSizeInt>(settingBuffer.size()));

    // 2. Load Moc
    std::string mocPath = dir + "/" + modelSetting.GetModelFileName();
    _mocBuffer = LoadFileAsBytes(mocPath);
    
    if (_mocBuffer.empty()) {
        std::cerr << "[LAppModel] Failed to load moc file: " << mocPath << std::endl;
        return;
    }
    
    LoadModel(_mocBuffer.data(), static_cast<Csm::csmSizeInt>(_mocBuffer.size()));

    // 3. Create Renderer
    CreateRenderer();
    GetRenderer()->Initialize(_model);
    
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        std::cerr << "[LAppModel] Renderer Initialize Error: " << err << std::endl;
    }

    // 4. Load Textures
    Csm::csmInt32 textureCount = modelSetting.GetTextureCount();
    for (Csm::csmInt32 i = 0; i < textureCount; i++)
    {
        std::string texturePath = dir + "/" + modelSetting.GetTextureFileName(i);
        LoadTexture(i, texturePath);
    }

    // 5. Load Hit Areas
    Csm::csmInt32 hitAreaCount = modelSetting.GetHitAreasCount();
    for (Csm::csmInt32 i = 0; i < hitAreaCount; ++i)
    {
        CubismIdHandle hitAreaId = modelSetting.GetHitAreaId(i);
        const Csm::csmChar* hitAreaName = modelSetting.GetHitAreaName(i);
        
        HitArea hitArea;
        hitArea.Id = hitAreaId;
        hitArea.Name = hitAreaName;
        _hitAreas.PushBack(hitArea);
        
        std::cout << "[LAppModel] HitArea Loaded: " << hitAreaName << std::endl;
    }
}

void LAppModel::LoadTexture(Csm::csmInt32 modelTextureIndex, const std::string& texturePath)
{
    int width, height, channels;
    unsigned char* image = stbi_load(texturePath.c_str(), &width, &height, &channels, 4); // Force RGBA
    
    if (!image) {
        std::cerr << "[LAppModel] Failed to load texture: " << texturePath << std::endl;
        return;
    }

    GLuint textureId;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);
    
    std::cout << "[LAppModel] Generated Texture ID: " << textureId << " for " << texturePath << std::endl;
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenerateMipmap(GL_TEXTURE_2D);

    stbi_image_free(image);

    auto renderer = GetRenderer();
    if (renderer) {
        renderer->BindTexture(modelTextureIndex, textureId);
    }
}

void LAppModel::Update()
{
    if (_model == NULL) return;

    _userTimeSeconds += 0.016f; // Approx 60 FPS

    _model->LoadParameters();
    _model->SaveParameters();
    
    // Test: Breathing
    Csm::csmFloat32 t = _userTimeSeconds * 2.0f * 3.14159f;
    _model->AddParameterValue(CubismFramework::GetIdManager()->GetId(ParamBreath), 0.5f + 0.5f * sin(t));

    _model->Update();
}

void LAppModel::Draw(Csm::CubismMatrix44& matrix)
{
    if (_model == NULL) return;

    Csm::CubismMatrix44 mvpMatrix = matrix;
    mvpMatrix.MultiplyByMatrix(_modelMatrix);

    GetRenderer()->SetMvpMatrix(&mvpMatrix);
    GetRenderer()->DrawModel();
}

bool LAppModel::HitTest(float x, float y)
{
    if (_opacity < 0.01f) return false; // Invisible

    // x, y are NDC (-1 to 1)
    // We need to convert them to Model Coordinates
    // Model = Inverse(View) * NDC
    // Since View = Scale * Translate
    // Model = Inverse(Translate) * Inverse(Scale) * NDC
    // ModelX = (x - trX) / scaleX
    // ModelY = (y - trY) / scaleY
    
    // Get scale and translate from _viewMatrix
    // Matrix array: 
    // 0  4  8  12 (trX)
    // 1  5  9  13 (trY)
    // 2  6  10 14
    // 3  7  11 15
    // Scale is usually at 0 and 5 (if no rotation)
    
    const csmFloat32* m = _viewMatrix.GetArray();
    float scaleX = m[0];
    float scaleY = m[5];
    float trX = m[12];
    float trY = m[13];
    
    if (scaleX == 0.0f || scaleY == 0.0f) return false;
    
    float modelX = (x - trX) / scaleX;
    float modelY = (y - trY) / scaleY;

    // Iterate through all hit areas
    for (Csm::csmUint32 i = 0; i < _hitAreas.GetSize(); i++)
    {
        if (IsHit(_hitAreas[i].Id, modelX, modelY))
        {
            return true;
        }
    }
    return false;
}
