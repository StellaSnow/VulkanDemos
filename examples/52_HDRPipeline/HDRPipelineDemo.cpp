#include "Common/Common.h"
#include "Common/Log.h"

#include "Demo/DVKCommon.h"

#include "Math/Vector4.h"
#include "Math/Matrix4x4.h"

#include <vector>

class HDRPipelineDemo : public DemoBase
{
public:
	HDRPipelineDemo(int32 width, int32 height, const char* title, const std::vector<std::string>& cmdLine)
		: DemoBase(width, height, title, cmdLine)
	{

	}

	virtual ~HDRPipelineDemo()
	{

	}

	virtual bool PreInit() override
	{
		return true;
	}

	virtual bool Init() override
	{
		DemoBase::Setup();
		DemoBase::Prepare();

		CreateGUI();
		InitParmas();
		CreateSourceRT();
		CreateBrightRT();
		CreateBlurRT();
		CreateLuminanceRT();
		LoadAssets();
		
		m_Ready = true;
		return true;
	}

	virtual void Exist() override
	{
		DestroyAssets();
		DestroyGUI();
		DemoBase::Release();
	}

	virtual void Loop(float time, float delta) override
	{
		if (!m_Ready) {
			return;
		}
		Draw(time, delta);
	}

private:

	struct ModelViewProjectionBlock
	{
		Matrix4x4 model;
		Matrix4x4 view;
		Matrix4x4 proj;
	};

	struct ParamBlock
	{
		Vector4 intensity;
	};

	void Draw(float time, float delta)
	{
		int32 bufferIndex = DemoBase::AcquireBackbufferIndex();

		UpdateFPS(time, delta);

		bool hovered = UpdateUI(time, delta);
		if (!hovered) {
			m_ViewCamera.Update(time, delta);
		}

		SetupCommandBuffers(bufferIndex);
		DemoBase::Present(bufferIndex);
	}

	bool UpdateUI(float time, float delta)
	{
		m_GUI->StartFrame();

		{
			ImGui::SetNextWindowPos(ImVec2(0, 0));
			ImGui::SetNextWindowSize(ImVec2(0, 0), ImGuiSetCond_FirstUseEver);
			ImGui::Begin("HDRPipelineDemo", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

			ImGui::SliderFloat("Intensity", &m_ParamData.intensity.x, 1.0f, 5.0f);

			ImGui::Text("%.3f ms/frame (%.1f FPS)", 1000.0f / m_LastFPS, m_LastFPS);
			ImGui::End();
		}

		bool hovered = ImGui::IsAnyWindowHovered() || ImGui::IsAnyItemHovered() || ImGui::IsRootWindowOrAnyChildHovered();

		m_GUI->EndFrame();
		m_GUI->Update();

		return hovered;
	}

	void CreateLuminanceRT()
	{
        // down sample
        m_TexLuminances[6] = vk_demo::DVKTexture::CreateRenderTarget(
			m_VulkanDevice,
			VK_FORMAT_R16G16_SFLOAT,
			VK_IMAGE_ASPECT_COLOR_BIT,
			m_TexSourceColor->width / 4, m_TexSourceColor->height / 4,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
		);
        
        // Luminance
        for (int32 i = 0; i < 6; ++i)
        {
            m_TexLuminances[i] = vk_demo::DVKTexture::CreateRenderTarget(
                m_VulkanDevice,
                VK_FORMAT_R16G16_SFLOAT,
                VK_IMAGE_ASPECT_COLOR_BIT,
                MMath::Pow(3, i), MMath::Pow(3, i),
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
            );
        }
        
        for (int32 i = 0; i < 6; ++i) {
            m_TexLuminances[i]->UpdateSampler(
                  VK_FILTER_NEAREST, VK_FILTER_NEAREST,
                  VK_SAMPLER_MIPMAP_MODE_NEAREST,
                  VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
            );
        }
        
        // render target pass
        for (int32 i = 0; i < 7; ++i)
        {
            vk_demo::DVKRenderPassInfo rttInfo(
            	m_TexLuminances[i], VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, nullptr
            );
            m_RTLuminances[i] = vk_demo::DVKRenderTarget::Create(m_VulkanDevice, rttInfo);
        }
        
        // down sample shader
        m_LuminanceDowmSampleShader = vk_demo::DVKShader::Create(
			m_VulkanDevice,
			true,
			"assets/shaders/52_HDRPipeline/luminanceDownsample.vert.spv",
			"assets/shaders/52_HDRPipeline/luminanceDownsample.frag.spv"
		);
        
        m_LuminanceMaterials[6] = vk_demo::DVKMaterial::Create(
			m_VulkanDevice,
			m_RTLuminances[6]->GetRenderPass(),
			m_PipelineCache,
			m_LuminanceDowmSampleShader
		);
		m_LuminanceMaterials[6]->PreparePipeline();
		m_LuminanceMaterials[6]->SetTexture("originTexture", m_TexSourceColor);
        
        // luminance shader
        m_LuminanceShader = vk_demo::DVKShader::Create(
			m_VulkanDevice,
			true,
			"assets/shaders/52_HDRPipeline/luminance.vert.spv",
			"assets/shaders/52_HDRPipeline/luminance.frag.spv"
		);
        
        for (int32 i = 0; i < 6; ++i)
        {
            m_LuminanceMaterials[i] = vk_demo::DVKMaterial::Create(
                m_VulkanDevice,
                m_RTLuminances[i]->GetRenderPass(),
                m_PipelineCache,
                m_LuminanceShader
            );
            m_LuminanceMaterials[i]->PreparePipeline();
            m_LuminanceMaterials[i]->SetTexture("originTexture", m_TexLuminances[i + 1]);
        }
	}
    
	void CreateBlurRT()
	{
		// blurH
		m_TexBlurH = vk_demo::DVKTexture::CreateRenderTarget(
			m_VulkanDevice,
			m_TexBright->format, 
			VK_IMAGE_ASPECT_COLOR_BIT,
			m_TexBright->width, m_TexBright->height,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
		);
		
		vk_demo::DVKRenderPassInfo rttInfoH(
			m_TexBlurH, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, nullptr
		);
		m_RTBlurH = vk_demo::DVKRenderTarget::Create(m_VulkanDevice, rttInfoH);

		m_BlurHShader = vk_demo::DVKShader::Create(
			m_VulkanDevice,
			true,
			"assets/shaders/52_HDRPipeline/blurH.vert.spv",
			"assets/shaders/52_HDRPipeline/blurH.frag.spv"
		);

		m_BlurHMaterial = vk_demo::DVKMaterial::Create(
			m_VulkanDevice,
			m_RTBlurH->GetRenderPass(),
			m_PipelineCache,
			m_BlurHShader
		);
		m_BlurHMaterial->PreparePipeline();
		m_BlurHMaterial->SetTexture("originTexture", m_TexBright);

		// blurV
		m_TexBlurV = vk_demo::DVKTexture::CreateRenderTarget(
			m_VulkanDevice,
			m_TexBlurH->format, 
			VK_IMAGE_ASPECT_COLOR_BIT,
			m_TexBlurH->width, m_TexBlurH->height,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
		);

		vk_demo::DVKRenderPassInfo rttInfoV(
			m_TexBlurV, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, nullptr
		);
		m_RTBlurV = vk_demo::DVKRenderTarget::Create(m_VulkanDevice, rttInfoV);

		m_BlurVShader = vk_demo::DVKShader::Create(
			m_VulkanDevice,
			true,
			"assets/shaders/52_HDRPipeline/blurV.vert.spv",
			"assets/shaders/52_HDRPipeline/blurV.frag.spv"
		);

		m_BlurVMaterial = vk_demo::DVKMaterial::Create(
			m_VulkanDevice,
			m_RTBlurV->GetRenderPass(),
			m_PipelineCache,
			m_BlurVShader
		);
		m_BlurVMaterial->PreparePipeline();
		m_BlurVMaterial->SetTexture("originTexture", m_TexBlurH);
	}

	void CreateBrightRT()
	{
		m_TexBright = vk_demo::DVKTexture::CreateRenderTarget(
			m_VulkanDevice,
			VK_FORMAT_R16G16B16A16_SFLOAT, 
			VK_IMAGE_ASPECT_COLOR_BIT,
			m_FrameWidth / 4.0f, m_FrameHeight / 4.0f,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
		);

		vk_demo::DVKRenderPassInfo rttInfo(
			m_TexBright, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, nullptr
		);
		m_RTBright = vk_demo::DVKRenderTarget::Create(m_VulkanDevice, rttInfo);

		m_BrightShader = vk_demo::DVKShader::Create(
			m_VulkanDevice,
			true,
			"assets/shaders/52_HDRPipeline/bright.vert.spv",
			"assets/shaders/52_HDRPipeline/bright.frag.spv"
		);

		m_BrightMaterial = vk_demo::DVKMaterial::Create(
			m_VulkanDevice,
			m_RTBright->GetRenderPass(),
			m_PipelineCache,
			m_BrightShader
		);
		m_BrightMaterial->PreparePipeline();
		m_BrightMaterial->SetTexture("originTexture", m_TexSourceColor);
	}

	void CreateSourceRT()
	{
		m_TexSourceColor = vk_demo::DVKTexture::CreateRenderTarget(
			m_VulkanDevice,
			VK_FORMAT_R16G16B16A16_SFLOAT, 
			VK_IMAGE_ASPECT_COLOR_BIT,
			m_FrameWidth, m_FrameHeight,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
		);

		m_TexSourceDepth = vk_demo::DVKTexture::CreateRenderTarget(
			m_VulkanDevice,
			PixelFormatToVkFormat(m_DepthFormat, false),
			VK_IMAGE_ASPECT_DEPTH_BIT,
			m_FrameWidth, m_FrameHeight,
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
		);

		vk_demo::DVKRenderPassInfo rttInfo(
			m_TexSourceColor, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
			m_TexSourceDepth, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE
		);
		m_RTSource = vk_demo::DVKRenderTarget::Create(m_VulkanDevice, rttInfo);
	}

	void LoadAssets()
	{
		vk_demo::DVKCommandBuffer* cmdBuffer = vk_demo::DVKCommandBuffer::Create(m_VulkanDevice, m_CommandPool);

		// fullscreen
		m_Quad = vk_demo::DVKDefaultRes::fullQuad;

		// scene model
		m_SceneModel = vk_demo::DVKModel::LoadFromFile(
			"assets/models/Portal/Portal_FInal.fbx",
			m_VulkanDevice,
			cmdBuffer,
			{ 
				VertexAttribute::VA_Position,
				VertexAttribute::VA_UV0,
				VertexAttribute::VA_Normal
			}
		);

		m_SceneShader = vk_demo::DVKShader::Create(
			m_VulkanDevice,
			true,
			"assets/shaders/52_HDRPipeline/obj.vert.spv",
			"assets/shaders/52_HDRPipeline/obj.frag.spv"
		);

		const char* textures[2] = {
			"assets/models/Portal/Portal_Main.jpg",
			"assets/models/Portal/Portal1.png"
		};

		for (int32 i = 0; i < 2; ++i)
		{
			m_SceneTextures[i] = vk_demo::DVKTexture::Create2D(
				textures[i],
				m_VulkanDevice,
				cmdBuffer
			);

			m_SceneMaterials[i] = vk_demo::DVKMaterial::Create(
				m_VulkanDevice,
				m_RTSource->GetRenderPass(),
				m_PipelineCache,
				m_SceneShader
			);
			m_SceneMaterials[i]->PreparePipeline();
			m_SceneMaterials[i]->SetTexture("diffuseMap", m_SceneTextures[i]);
		}

		// for debug
		m_DebugShader = vk_demo::DVKShader::Create(
			m_VulkanDevice,
			true,
			"assets/shaders/52_HDRPipeline/debug.vert.spv",
			"assets/shaders/52_HDRPipeline/debug.frag.spv"
		);

		m_DebugMaterial = vk_demo::DVKMaterial::Create(
			m_VulkanDevice,
			m_RenderPass,
			m_PipelineCache,
			m_DebugShader
		);
		m_DebugMaterial->PreparePipeline();
		m_DebugMaterial->SetTexture("originTexture", m_TexLuminances[3]);

		delete cmdBuffer;
	}
    
	void DestroyAssets()
	{
		delete m_SceneModel;
		delete m_SceneShader;

		delete m_DebugShader;
		delete m_DebugMaterial;

		for (int32 i = 0; i < 2; ++i)
		{
			delete m_SceneTextures[i];
			delete m_SceneMaterials[i];
		}

		// source
		{
			delete m_TexSourceColor;
			delete m_TexSourceDepth;
			delete m_RTSource;
		}

		// bright
		{
			delete m_TexBright;
			delete m_RTBright;
			delete m_BrightShader;
			delete m_BrightMaterial;
		}

		// blur h
		{
			delete m_TexBlurH;
			delete m_RTBlurH;
			delete m_BlurHShader;
			delete m_BlurHMaterial;
		}

		// blur v
		{
			delete m_TexBlurV;
			delete m_RTBlurV;
			delete m_BlurVShader;
			delete m_BlurVMaterial;
		}

		// luminance
		{
			for (int32 i = 0; i < 7; ++i)
			{
				delete m_TexLuminances[i];
				delete m_RTLuminances[i];
				delete m_LuminanceMaterials[i];
			}
			delete m_LuminanceDowmSampleShader;
			delete m_LuminanceShader;
		}
	}

	void RenderScene(VkCommandBuffer commandBuffer)
	{
		vk_demo::DVKMaterial* materials[4] = {
			m_SceneMaterials[1],
			m_SceneMaterials[0],
			m_SceneMaterials[1],
			m_SceneMaterials[1]
		};

		float params[4] = {
			m_ParamData.intensity.x, 
			1, 
			m_ParamData.intensity.x, 
			m_ParamData.intensity.x
		};
		
		for (int32 i = 0; i < m_SceneModel->meshes.size(); ++i)
		{
			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, materials[i]->GetPipeline());

			materials[i]->BeginFrame();

			m_MVPParam.model = m_SceneModel->meshes[i]->linkNode->GetGlobalMatrix();
			m_MVPParam.view  = m_ViewCamera.GetView();
			m_MVPParam.proj  = m_ViewCamera.GetProjection();

			m_ParamData.intensity.x = params[i];

			materials[i]->BeginObject();
			materials[i]->SetLocalUniform("uboMVP",      &m_MVPParam,         sizeof(ModelViewProjectionBlock));
			materials[i]->SetLocalUniform("param",       &m_ParamData,        sizeof(ParamBlock));
			materials[i]->EndObject();

			materials[i]->BindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, 0);
			m_SceneModel->meshes[i]->BindDrawCmd(commandBuffer);

			materials[i]->EndFrame();
		}

		// restore
		m_ParamData.intensity.x = params[0];
	}

	void SourcePass(VkCommandBuffer commandBuffer)
	{
		m_RTSource->BeginRenderPass(commandBuffer);
		RenderScene(commandBuffer);
		m_RTSource->EndRenderPass(commandBuffer);
	}

	void BrightPass(VkCommandBuffer commandBuffer)
	{
		m_RTBright->BeginRenderPass(commandBuffer);

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_BrightMaterial->GetPipeline());
		m_BrightMaterial->BindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, 0);
		m_Quad->meshes[0]->BindDrawCmd(commandBuffer);

		m_RTBright->EndRenderPass(commandBuffer);
	}

	void BlurHPass(VkCommandBuffer commandBuffer)
	{
		m_RTBlurH->BeginRenderPass(commandBuffer);

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_BlurHMaterial->GetPipeline());
		m_BlurHMaterial->BindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, 0);
		m_Quad->meshes[0]->BindDrawCmd(commandBuffer);

		m_RTBlurH->EndRenderPass(commandBuffer);
	}
    
    void LuminancePass(VkCommandBuffer commandBuffer)
    {
        // downsample first
        // Luminance one by one
        for (int32 i = 6; i >= 0; --i)
        {
            m_RTLuminances[i]->BeginRenderPass(commandBuffer);
            
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_LuminanceMaterials[i]->GetPipeline());
            m_LuminanceMaterials[i]->BindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, 0);
            m_Quad->meshes[0]->BindDrawCmd(commandBuffer);
            
            m_RTLuminances[i]->EndRenderPass(commandBuffer);
        }
    }
    
	void BlurVPass(VkCommandBuffer commandBuffer)
	{
		m_RTBlurV->BeginRenderPass(commandBuffer);

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_BlurVMaterial->GetPipeline());
		m_BlurVMaterial->BindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, 0);
		m_Quad->meshes[0]->BindDrawCmd(commandBuffer);

		m_RTBlurV->EndRenderPass(commandBuffer);
	}

	void RenderFinal(VkCommandBuffer commandBuffer, int32 backBufferIndex)
	{
		VkViewport viewport = {};
		viewport.x        = 0;
		viewport.y        = m_FrameHeight;
		viewport.width    = m_FrameWidth;
		viewport.height   = -(float)m_FrameHeight;    // flip y axis
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

		VkRect2D scissor = {};
		scissor.extent.width  = m_FrameWidth;
		scissor.extent.height = m_FrameHeight;
		scissor.offset.x = 0;
		scissor.offset.y = 0;

		VkClearValue clearValues[2];
		clearValues[0].color        = { { 0.2f, 0.2f, 0.2f, 1.0f } };
		clearValues[1].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo renderPassBeginInfo;
		ZeroVulkanStruct(renderPassBeginInfo, VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
		renderPassBeginInfo.renderPass               = m_RenderPass;
		renderPassBeginInfo.framebuffer              = m_FrameBuffers[backBufferIndex];
		renderPassBeginInfo.clearValueCount          = 2;
		renderPassBeginInfo.pClearValues             = clearValues;
		renderPassBeginInfo.renderArea.offset.x      = 0;
		renderPassBeginInfo.renderArea.offset.y      = 0;
		renderPassBeginInfo.renderArea.extent.width  = m_FrameWidth;
		renderPassBeginInfo.renderArea.extent.height = m_FrameHeight;
		vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
		vkCmdSetScissor(commandBuffer,  0, 1, &scissor);

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_DebugMaterial->GetPipeline());
		m_DebugMaterial->BindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, 0);
		m_Quad->meshes[0]->BindDrawCmd(commandBuffer);

		m_GUI->BindDrawCmd(commandBuffer, m_RenderPass);

		vkCmdEndRenderPass(commandBuffer);
	}

	void SetupCommandBuffers(int32 backBufferIndex)
	{
		VkCommandBuffer commandBuffer = m_CommandBuffers[backBufferIndex];

		VkCommandBufferBeginInfo cmdBeginInfo;
		ZeroVulkanStruct(cmdBeginInfo, VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
		VERIFYVULKANRESULT(vkBeginCommandBuffer(commandBuffer, &cmdBeginInfo));

		SourcePass(commandBuffer);
		BrightPass(commandBuffer);
		BlurHPass(commandBuffer);
		BlurVPass(commandBuffer);
        LuminancePass(commandBuffer);

		RenderFinal(commandBuffer, backBufferIndex);
		
		VERIFYVULKANRESULT(vkEndCommandBuffer(commandBuffer));
	}

	void InitParmas()
	{
		m_ParamData.intensity.x = 2.5f;

		m_ViewCamera.SetPosition(0, 5.0f, -30.0f);
		m_ViewCamera.LookAt(0, 5.0f, 0);
		m_ViewCamera.Perspective(PI / 4, (float)GetWidth(), (float)GetHeight(), 1.0f, 1500.0f);
	}

	void CreateGUI()
	{
		m_GUI = new ImageGUIContext();
		m_GUI->Init("assets/fonts/Ubuntu-Regular.ttf");
	}

	void DestroyGUI()
	{
		m_GUI->Destroy();
		delete m_GUI;
	}

private:

	bool 						m_Ready = false;

	vk_demo::DVKModel*			m_Quad = nullptr;
	vk_demo::DVKMaterial*	    m_DebugMaterial;
	vk_demo::DVKShader*		    m_DebugShader;

	vk_demo::DVKModel*			m_SceneModel = nullptr;
	vk_demo::DVKShader*			m_SceneShader = nullptr;
	vk_demo::DVKTexture*		m_SceneTextures[2];
	vk_demo::DVKMaterial*		m_SceneMaterials[2];

	// source
	vk_demo::DVKTexture*		m_TexSourceColor = nullptr;
	vk_demo::DVKTexture*		m_TexSourceDepth = nullptr;

	vk_demo::DVKRenderTarget*	m_RTSource = nullptr;

	// bright pass
	vk_demo::DVKTexture*		m_TexBright = nullptr;
	vk_demo::DVKRenderTarget*	m_RTBright = nullptr;
	vk_demo::DVKShader*			m_BrightShader = nullptr;
	vk_demo::DVKMaterial*		m_BrightMaterial = nullptr;

	// blur h pass
	vk_demo::DVKTexture*		m_TexBlurH = nullptr;
	vk_demo::DVKRenderTarget*	m_RTBlurH = nullptr;
	vk_demo::DVKShader*			m_BlurHShader = nullptr;
	vk_demo::DVKMaterial*		m_BlurHMaterial = nullptr;
	
	// blur v pass
	vk_demo::DVKTexture*		m_TexBlurV = nullptr;
	vk_demo::DVKRenderTarget*	m_RTBlurV = nullptr;
	vk_demo::DVKShader*			m_BlurVShader = nullptr;
	vk_demo::DVKMaterial*		m_BlurVMaterial = nullptr;

	// luminance
	vk_demo::DVKTexture*		m_TexLuminances[7];
	vk_demo::DVKRenderTarget*	m_RTLuminances[7];
	vk_demo::DVKShader*			m_LuminanceDowmSampleShader = nullptr;
	vk_demo::DVKShader*			m_LuminanceShader = nullptr;
	vk_demo::DVKMaterial*		m_LuminanceMaterials[7];

	vk_demo::DVKCamera		    m_ViewCamera;

	ModelViewProjectionBlock	m_MVPParam;
	ParamBlock					m_ParamData;

	ImageGUIContext*			m_GUI = nullptr;
};

std::shared_ptr<AppModuleBase> CreateAppMode(const std::vector<std::string>& cmdLine)
{
	return std::make_shared<HDRPipelineDemo>(1400, 900, "HDRPipelineDemo", cmdLine);
}
