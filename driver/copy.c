#include "common.h"

#include "declarations.h"
#include "vkExtFunctions.h"

//TODO change allocations to pool allocator

//TODO
// ETC1 is arranged as 64-bit blocks, where each block
// is 4x4 pixels.  Texture tiling operates on the
// 64-bit block the way it would an uncompressed
// pixels.

//TODO
// Cube map faces appear as whole miptrees at a page-aligned offset
// from the first face's miptree.



uint32_t getMemoryTypeIndex(VkPhysicalDeviceMemoryProperties deviceMemoryProperties, uint32_t typeBits, VkMemoryPropertyFlags properties)
{
	// Iterate over all memory types available for the device used in this example
	for (uint32_t i = 0; i < deviceMemoryProperties.memoryTypeCount; i++)
	{
		if ((typeBits & 1) == 1)
		{
			if ((deviceMemoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
			{
				return i;
			}
		}
		typeBits >>= 1;
	}

	assert(0);
}

void createFullscreenQuad(VkDevice device, VkBuffer* fsqVertexBuffer, VkDeviceMemory* fsqVertexBufferMemory)
{
	VkMemoryRequirements mr;

	{ //create fsq vertex buffer
		unsigned vboSize = sizeof(float) * 4 * 3 * 2; //4 * 3 x vec2

		VkBufferCreateInfo ci = {};
		ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		ci.size = vboSize;
		ci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

		VkResult res = rpi_vkCreateBuffer(device, &ci, 0, fsqVertexBuffer);

		rpi_vkGetBufferMemoryRequirements(device, *fsqVertexBuffer, &mr);

		VkPhysicalDeviceMemoryProperties pdmp;
		rpi_vkGetPhysicalDeviceMemoryProperties(((_device*)device)->dev, &pdmp);

		VkMemoryAllocateInfo mai = {};
		mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		mai.allocationSize = mr.size;
		mai.memoryTypeIndex = getMemoryTypeIndex(pdmp, mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

		res = rpi_vkAllocateMemory(device, &mai, 0, fsqVertexBufferMemory);

		float vertices[] =
		{
			-1, -1,		0, 1,
			1, -1,		1, 1,
			1, 1,		1, 0,

			1, 1,		1, 0,
			-1, 1,		0, 0,
			-1, -1,		0, 1
		};

		void* data;
		res = rpi_vkMapMemory(device, *fsqVertexBufferMemory, 0, mr.size, 0, &data);
		memcpy(data, vertices, vboSize);
		rpi_vkUnmapMemory(device, *fsqVertexBufferMemory);

		res = rpi_vkBindBufferMemory(device, *fsqVertexBuffer, *fsqVertexBufferMemory, 0);
	}
}

void createDescriptorPool(VkDevice device, VkDescriptorPool* descriptorPool)
{
	VkDescriptorPoolSize descriptorPoolSizes[2];
	descriptorPoolSizes[0].descriptorCount = 100;
	descriptorPoolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
	descriptorPoolSizes[1].descriptorCount = 100;
	descriptorPoolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

	VkDescriptorPoolCreateInfo descriptorPoolCI = {};
	descriptorPoolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	descriptorPoolCI.poolSizeCount = 2;
	descriptorPoolCI.pPoolSizes = descriptorPoolSizes;
	descriptorPoolCI.maxSets = 200;
	descriptorPoolCI.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

	rpi_vkCreateDescriptorPool(device, &descriptorPoolCI, 0, descriptorPool);
}

void createDescriptorSetLayouts(VkDevice device, VkDescriptorSetLayout* bufferDsl, VkDescriptorSetLayout* textureDsl)
{
	assert(device);
	assert(bufferDsl);
	assert(textureDsl);

	//create blit dsl
	VkDescriptorSetLayoutBinding setLayoutBinding = {};
	setLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
	setLayoutBinding.binding = 0;
	setLayoutBinding.descriptorCount = 1;
	setLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo descriptorLayoutCI = {};
	descriptorLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descriptorLayoutCI.bindingCount = 1;
	descriptorLayoutCI.pBindings = &setLayoutBinding;

	rpi_vkCreateDescriptorSetLayout(device, &descriptorLayoutCI, 0, bufferDsl);

	setLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	rpi_vkCreateDescriptorSetLayout(device, &descriptorLayoutCI, 0, textureDsl);
}

void createSampler(VkDevice device, VkSampler* nearestTextureSampler, VkSampler* linearTextureSampler)
{
	VkSamplerCreateInfo sampler = {};
	sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sampler.magFilter = VK_FILTER_NEAREST;
	sampler.minFilter = VK_FILTER_NEAREST;
	sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sampler.mipLodBias = 0.0f;
	sampler.compareOp = VK_COMPARE_OP_NEVER;
	sampler.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
	rpi_vkCreateSampler(device, &sampler, 0, nearestTextureSampler);

	sampler.magFilter = VK_FILTER_LINEAR;
	sampler.minFilter = VK_FILTER_LINEAR;
	rpi_vkCreateSampler(device, &sampler, 0, linearTextureSampler);
}

void createRendertarget(VkDevice device, uint32_t baseLayer, uint32_t baseMip, uint32_t width, uint32_t height, VkImage textureImage, VkImageView* textureView, VkRenderPass* offscreenRenderPass, VkFramebuffer* offscreenFramebuffer)
{
	_image* img = textureImage;
	VkFormat format = img->format;

	printf("\nCopy Create RT\n");
	printf("baseLayer %u\n", baseLayer);
	printf("baseMip %u\n", baseMip);
	printf("width %u\n", width);
	printf("height %u\n", height);

	//we can't render to an ETC1 texture, so we'll just stick with RGBA8 for now
	if(img->format == VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK)
	{
		format = VK_FORMAT_R8G8B8A8_UNORM;
	}

	VkImageViewCreateInfo view = {};
	view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view.format = format;
	view.components.a = VK_COMPONENT_SWIZZLE_A;
	view.components.b = VK_COMPONENT_SWIZZLE_B;
	view.components.g = VK_COMPONENT_SWIZZLE_G;
	view.components.r = VK_COMPONENT_SWIZZLE_R;
	view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view.subresourceRange.baseMipLevel = baseMip;
	view.subresourceRange.baseArrayLayer = baseLayer;
	view.subresourceRange.layerCount = 1;
	view.subresourceRange.levelCount = 1;
	view.image = textureImage;
	rpi_vkCreateImageView(device, &view, 0, textureView);

	VkAttachmentDescription attachmentDescription = {};
	attachmentDescription.format = format;
	attachmentDescription.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachmentDescription.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	attachmentDescription.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachmentDescription.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachmentDescription.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachmentDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachmentDescription.samples = VK_SAMPLE_COUNT_1_BIT;

	VkAttachmentReference colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

	VkSubpassDescription subpassDescription = {};
	subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpassDescription.colorAttachmentCount = 1;
	subpassDescription.pColorAttachments = &colorReference;

	VkSubpassDependency dependencies[2];
	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass = 0;
	dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
	dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	dependencies[1].srcSubpass = 0;
	dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = 1;
	renderPassInfo.pAttachments = &attachmentDescription;
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpassDescription;
	renderPassInfo.dependencyCount = 2;
	renderPassInfo.pDependencies = dependencies;

	rpi_vkCreateRenderPass(device, &renderPassInfo, 0, offscreenRenderPass);

	VkImageView attachments = *textureView;

	VkFramebufferCreateInfo framebufferCreateInfo = {};
	framebufferCreateInfo.renderPass = *offscreenRenderPass;
	framebufferCreateInfo.attachmentCount = 1;
	framebufferCreateInfo.pAttachments = &attachments;
	framebufferCreateInfo.width = width;
	framebufferCreateInfo.height = height;
	framebufferCreateInfo.layers = 1;

	rpi_vkCreateFramebuffer(device, &framebufferCreateInfo, 0, offscreenFramebuffer);
}

void createPipeline(VkDevice device, uint32_t needTexcoords, uint32_t numVertUniforms, uint32_t numFragUniforms, VkShaderModule blitShaderModule, VkDescriptorSetLayout blitDsl, VkPipelineLayout* blitPipelineLayout, VkRenderPass offscreenRenderPass, VkPipeline* blitPipeline)
{
	VkVertexInputBindingDescription vertexInputBindingDescription =
	{
		0,
		sizeof(float) * 2 * 2,
		VK_VERTEX_INPUT_RATE_VERTEX
	};

	VkVertexInputAttributeDescription vertexInputAttributeDescription[2];

	if(!needTexcoords)
	{
		vertexInputAttributeDescription[0].binding = 0;
		vertexInputAttributeDescription[0].location = 0;
		vertexInputAttributeDescription[0].offset = 0;
		vertexInputAttributeDescription[0].format = VK_FORMAT_R32G32_SFLOAT;
	}
	else
	{
		vertexInputAttributeDescription[0].binding = 0;
		vertexInputAttributeDescription[0].location = 0;
		vertexInputAttributeDescription[0].offset = 0;
		vertexInputAttributeDescription[0].format = VK_FORMAT_R32G32_SFLOAT;

		vertexInputAttributeDescription[1].binding = 0;
		vertexInputAttributeDescription[1].location = 1;
		vertexInputAttributeDescription[1].offset = sizeof(float) * 2;
		vertexInputAttributeDescription[1].format = VK_FORMAT_R32G32_SFLOAT;
	}

	VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
	vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputInfo.vertexAttributeDescriptionCount = needTexcoords ? 2 : 1;
	vertexInputInfo.pVertexAttributeDescriptions = vertexInputAttributeDescription;
	vertexInputInfo.vertexBindingDescriptionCount = 1;
	vertexInputInfo.pVertexBindingDescriptions = &vertexInputBindingDescription;

	VkPipelineInputAssemblyStateCreateInfo pipelineIACreateInfo = {};
	pipelineIACreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	pipelineIACreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineRasterizationStateCreateInfo rastCreateInfo = {};
	rastCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rastCreateInfo.polygonMode = VK_POLYGON_MODE_FILL;
	rastCreateInfo.cullMode = VK_CULL_MODE_NONE;
	rastCreateInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rastCreateInfo.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo pipelineMSCreateInfo = {};
	pipelineMSCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;

	VkPipelineColorBlendAttachmentState blendAttachState = {};
	blendAttachState.colorWriteMask = 0xf;
	blendAttachState.blendEnable = false;

	VkPipelineColorBlendStateCreateInfo blendCreateInfo = {};
	blendCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blendCreateInfo.attachmentCount = 1;
	blendCreateInfo.pAttachments = &blendAttachState;

	VkPipelineDepthStencilStateCreateInfo depthStencilState = {};
	depthStencilState.depthTestEnable = false;
	depthStencilState.stencilTestEnable = false;

	//create blit pipeline
	VkPushConstantRange pushConstantRanges[2];
	pushConstantRanges[0].offset = 0;
	pushConstantRanges[0].size = numVertUniforms * 4; //n * 32bits
	pushConstantRanges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	pushConstantRanges[1].offset = 0;
	pushConstantRanges[1].size = numFragUniforms * 4; //n * 32bits
	pushConstantRanges[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkPipelineShaderStageCreateInfo shaderStageCreateInfo[2] = {};

	shaderStageCreateInfo[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStageCreateInfo[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	shaderStageCreateInfo[0].module = blitShaderModule;
	shaderStageCreateInfo[0].pName = "main";
	shaderStageCreateInfo[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStageCreateInfo[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shaderStageCreateInfo[1].module = blitShaderModule;
	shaderStageCreateInfo[1].pName = "main";

	VkPipelineLayoutCreateInfo pipelineLayoutCI = {};
	pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutCI.setLayoutCount = 1;
	pipelineLayoutCI.pSetLayouts = &blitDsl;
	pipelineLayoutCI.pushConstantRangeCount = 2;
	pipelineLayoutCI.pPushConstantRanges = &pushConstantRanges[0];
	rpi_vkCreatePipelineLayout(device, &pipelineLayoutCI, 0, blitPipelineLayout);

	VkDynamicState dynState = VK_DYNAMIC_STATE_VIEWPORT;

	VkPipelineDynamicStateCreateInfo pdsci = {};
	pdsci.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	pdsci.dynamicStateCount = 1;
	pdsci.pDynamicStates = &dynState;

	VkPipelineViewportStateCreateInfo pvsci = {};
	pvsci.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	pvsci.viewportCount = 0;
	pvsci.scissorCount = 0;

	VkGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.stageCount = 2;
	pipelineInfo.pStages = &shaderStageCreateInfo[0];
	pipelineInfo.pVertexInputState = &vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &pipelineIACreateInfo;
	pipelineInfo.pViewportState = &pvsci;
	pipelineInfo.pDynamicState = &pdsci;
	pipelineInfo.pRasterizationState = &rastCreateInfo;
	pipelineInfo.pMultisampleState = &pipelineMSCreateInfo;
	pipelineInfo.pColorBlendState = &blendCreateInfo;
	pipelineInfo.renderPass = offscreenRenderPass;
	pipelineInfo.basePipelineIndex = -1;
	pipelineInfo.pDepthStencilState = &depthStencilState;
	pipelineInfo.layout = *blitPipelineLayout;

	VkResult res = rpi_vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, blitPipeline);
}

void createBufferToTextureShaderModule(VkDevice device, VkShaderModule* blitShaderModule)
{
	char vs_asm_code[] =
			///0x40000000 = 2.0
			///uni = 1.0
			///rb0 = 2 - 1 = 1
			"sig_small_imm ; rx0 = fsub.ws.always(b, a, uni, 0x40000000) ; nop = nop(r0, r0) ;\n"
			///set up VPM read for subsequent reads
			///0x00201a00: 0000 0000 0010 0000 0001 1010 0000 0000
			///addr: 0
			///size: 32bit
			///packed
			///horizontal
			///stride=1
			///vectors to read = 2 (how many components)
			"sig_load_imm ; vr_setup = load32.always(0x00201a00) ; nop = load32.always() ;\n"
			///uni = viewportXScale
			///r0 = vpm * uni
			"sig_none ; nop = nop(r0, r0, vpm_read, uni) ; r0 = fmul.always(a, b) ;\n"
			///r1 = r0 * rb0 (1)
			"sig_none ; nop = nop(r0, r0, nop, rb0) ; r1 = fmul.always(r0, b) ;\n"
			///uni = viewportYScale
			///ra0.16a = int(r1), r2 = vpm * uni
			"sig_none ; rx0.16a = ftoi.always(r1, r1, vpm_read, uni) ; r2 = fmul.always(a, b) ;\n"
			///r3 = r2 * rb0
			"sig_none ; nop = nop(r0, r0, nop, rb0) ; r3 = fmul.always(r2, b) ;\n"
			///ra0.16b = int(r3)
			"sig_none ; rx0.16b = ftoi.always(r3, r3) ; nop = nop(r0, r0) ;\n"
			///set up VPM write for subsequent writes
			///0x00001a00: 0000 0000 0000 0000 0001 1010 0000 0000
			///addr: 0
			///size: 32bit
			///horizontal
			///stride = 1
			"sig_load_imm ; vw_setup = load32.always.ws(0x00001a00) ; nop = load32.always() ;\n"
			///shaded vertex format for PSE
			/// Ys and Xs
			///vpm = ra0
			"sig_none ; vpm = or.always(a, a, ra0, nop) ; nop = nop(r0, r0);\n"
			/// Zs
			///uni = 0.5
			///vpm = uni
			"sig_none ; vpm = or.always(a, a, uni, nop) ; nop = nop(r0, r0);\n"
			/// 1.0 / Wc
			///vpm = rb0 (1)
			"sig_none ; vpm = or.always(b, b, nop, rb0) ; nop = nop(r0, r0);\n"
			///END
			"sig_end ; nop = nop(r0, r0) ; nop = nop(r0, r0) ;\n"
			"sig_none ; nop = nop(r0, r0) ; nop = nop(r0, r0) ;\n"
			"sig_none ; nop = nop(r0, r0) ; nop = nop(r0, r0) ;\n"
				"\0";

	char cs_asm_code[] =
			///uni = 1.0
			///r3 = 2.0 - uni
			"sig_small_imm ; r3 = fsub.always(b, a, uni, 0x40000000) ; nop = nop(r0, r0);\n"
			"sig_load_imm ; vr_setup = load32.always(0x00201a00) ; nop = load32.always() ;\n"
			///r2 = vpm
			"sig_none ; r2 = or.always(a, a, vpm_read, nop) ; nop = nop(r0, r0);\n"
			"sig_load_imm ; vw_setup = load32.always.ws(0x00001a00) ; nop = load32.always() ;\n"
			///shaded coordinates format for PTB
			/// write Xc
			///r1 = vpm, vpm = r2
			"sig_none ; r1 = or.always(a, a, vpm_read, nop) ; vpm = v8min.always(r2, r2);\n"
			/// write Yc
			///uni = viewportXscale
			///vpm = r1, r2 = r2 * uni
			"sig_none ; vpm = or.always(r1, r1, uni, nop) ; r2 = fmul.always(r2, a);\n"
			///uni = viewportYscale
			///r1 = r1 * uni
			"sig_none ; nop = nop(r0, r0, uni, nop) ; r1 = fmul.always(r1, a);\n"
			///r0 = r2 * r3
			"sig_none ; nop = nop(r0, r0) ; r0 = fmul.always(r2, r3);\n"
			///ra0.16a = r0, r1 = r1 * r3
			"sig_none ; rx0.16a = ftoi.always(r0, r0) ; r1 = fmul.always(r1, r3) ;\n"
			///ra0.16b = r1
			"sig_none ; rx0.16b = ftoi.always(r1, r1) ; nop = nop(r0, r0) ;\n"
			///write Zc
			///vpm = 0
			"sig_small_imm ; vpm = or.always(b, b, nop, 0) ; nop = nop(r0, r0) ;\n"
			///write Wc
			///vpm = 1.0
			"sig_small_imm ; vpm = or.always(b, b, nop, 0x3f800000) ; nop = nop(r0, r0) ;\n"
			///write Ys and Xs
			///vpm = ra0
			"sig_none ; vpm = or.always(a, a, ra0, nop) ; nop = nop(r0, r0) ;\n"
			///write Zs
			///uni = 0.5
			///vpm = uni
			"sig_none ; vpm = or.always(a, a, uni, nop) ; nop = nop(r0, r0) ;\n"
			///write 1/Wc
			///vpm = r3
			"sig_none ; vpm = or.always(r3, r3) ; nop = nop(r0, r0) ;\n"
			///END
			"sig_end ; nop = nop(r0, r0) ; nop = nop(r0, r0) ;\n"
			"sig_none ; nop = nop(r0, r0) ; nop = nop(r0, r0) ;\n"
			"sig_none ; nop = nop(r0, r0) ; nop = nop(r0, r0) ;\n"
				"\0";

	//clever: use small immedate -1 interpreted as 0xffffffff (white) to set color to white
	//"sig_small_imm ; tlb_color_all = or.always(b, b, nop, -1) ; nop = nop(r0, r0) ;"

	//8bit access
	//abcd
	//BGRA

	/**
	"General-memory lookups are performed by writing to just the ‘s’ parameter, using the absolute memory
	address. In this case no uniform is read. General-memory lookups always return a 32-bit value, and the bottom
	two bits of the address are ignored."
	/**/

	//blit buffer to texture (generic buffer read)
	char blit_fs_asm_code[] =
			///"sig_load_imm ; r2 = load32.always(0x44f00000) ; nop = load32() ;" //width = 1920.0
			"sig_none ; r2 = or.always(b, b, nop, uni) ; nop = nop(r0, r0) ;" //width
			"sig_none ; r1 = itof.always(b, b, x_pix, y_pix) ; nop = nop(r0, r0) ;" //FragCoord Y
			"sig_none ; r0 = itof.always(a, a, x_pix, y_pix) ; r1 = fmul.always(r1, r2) ;" //FragCoord X, r1 = Y * width
			"sig_none ; r0 = fadd.always(r0, r1) ; r0 = nop(r0, r0) ;" //r0 = Y * width + X
			"sig_none ; r0 = nop(r0, r0, nop, uni) ; r0 = fmul.always(r0, b) ;" //r0 = (Y * width + X) * pixelBpp
			"sig_small_imm ; nop = nop(r0, r0, nop, 0x3e000000) ; r0 = fmul.always(r0, b) ;" //r0 = ((Y * width + X) * pixelBpp) / 8
			"sig_none ; r0 = ftoi.always(r0, r0) ; nop = nop(r0, r0) ;" //convert to integer
			///write general mem access address
			///first argument must be clamped to [0...bufsize-4]
			///eg must do min(max(x,0), uni)
			///second argument must be a uniform (containing base address, which is 0)
			///writing tmu0_s signals that all coordinates are written
			"sig_small_imm ; r0 = max.always(r0, b, nop, 0) ; nop = nop(r0, r0) ;" //clamp general access
			"sig_none ; r0 = min.always(r0, b, nop, uni) ; nop = nop(r0, r0) ;" //uni = width * height * pixelBytes - pixelBytes
			"sig_none ; tmu0_s = add.always(r0, b, nop, uni) ; nop = nop(r0, r0) ;" //uni = 0
			///suspend thread (after 2 nops) to wait for TMU request to finish
			"sig_thread_switch ; nop = nop(r0, r0) ; nop = nop(r0, r0) ;"
			"sig_none ; nop = nop(r0, r0) ; nop = nop(r0, r0) ;"
			"sig_none ; nop = nop(r0, r0) ; nop = nop(r0, r0) ;"
			///read TMU0 request result to R4
			"sig_load_tmu0 ; nop = nop(r0, r0) ; nop = nop(r0, r0) ;"
			///when thread has been awakened, MOV from R4 to R0
			"sig_none ; r0 = fmax.pm.always.8a(r4, r4) ; nop = nop(r0, r0) ;"
			"sig_none ; r1 = fmax.pm.always.8b(r4, r4) ; r0.8a = v8min.always(r0, r0) ;"
			"sig_none ; r2 = fmax.pm.always.8c(r4, r4) ; r0.8b = v8min.always(r1, r1) ;"
			"sig_none ; r3 = fmax.pm.always.8d(r4, r4) ; r0.8c = v8min.always(r2, r2) ;"
			"sig_none ; nop = nop.pm(r0, r0) ; r0.8d = v8min.always(r3, r3) ;"
			"sig_none ; tlb_color_all = or.always(r0, r0) ; nop = nop(r0, r0) ;"
			"sig_end ; nop = nop(r0, r0) ; nop = nop(r0, r0) ;"
			"sig_none ; nop = nop(r0, r0) ; nop = nop(r0, r0) ;"
			"sig_unlock_score ; nop = nop(r0, r0) ; nop = nop(r0, r0) ;"
				"\0";

	char* blit_asm_strings[] =
	{
		(char*)cs_asm_code, (char*)vs_asm_code, (char*)blit_fs_asm_code, 0
	};

	VkRpiAssemblyMappingEXT blit_mappings[] = {
		//vertex shader uniforms
		{
			VK_RPI_ASSEMBLY_MAPPING_TYPE_PUSH_CONSTANT,
			VK_DESCRIPTOR_TYPE_MAX_ENUM, //descriptor type
			0, //descriptor set #
			0, //descriptor binding #
			0, //descriptor array element #
			0, //resource offset
			VK_SHADER_STAGE_VERTEX_BIT
		},
		{
			VK_RPI_ASSEMBLY_MAPPING_TYPE_PUSH_CONSTANT,
			VK_DESCRIPTOR_TYPE_MAX_ENUM, //descriptor type
			0, //descriptor set #
			0, //descriptor binding #
			0, //descriptor array element #
			4, //resource offset
			VK_SHADER_STAGE_VERTEX_BIT
		},
		{
			VK_RPI_ASSEMBLY_MAPPING_TYPE_PUSH_CONSTANT,
			VK_DESCRIPTOR_TYPE_MAX_ENUM, //descriptor type
			0, //descriptor set #
			0, //descriptor binding #
			0, //descriptor array element #
			8, //resource offset
			VK_SHADER_STAGE_VERTEX_BIT
		},
		{
			VK_RPI_ASSEMBLY_MAPPING_TYPE_PUSH_CONSTANT,
			VK_DESCRIPTOR_TYPE_MAX_ENUM, //descriptor type
			0, //descriptor set #
			0, //descriptor binding #
			0, //descriptor array element #
			12, //resource offset
			VK_SHADER_STAGE_VERTEX_BIT
		},


		//fragment shader uniforms
		{
			VK_RPI_ASSEMBLY_MAPPING_TYPE_DESCRIPTOR,
			VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, //descriptor type
			0, //descriptor set #
			0, //descriptor binding #
			0, //descriptor array element #
			0, //resource offset
			VK_SHADER_STAGE_FRAGMENT_BIT
		},
		{
			VK_RPI_ASSEMBLY_MAPPING_TYPE_PUSH_CONSTANT,
			VK_DESCRIPTOR_TYPE_MAX_ENUM, //descriptor type
			0, //descriptor set #
			0, //descriptor binding #
			0, //descriptor array element #
			0, //resource offset
			VK_SHADER_STAGE_FRAGMENT_BIT
		},
		{
			VK_RPI_ASSEMBLY_MAPPING_TYPE_PUSH_CONSTANT,
			VK_DESCRIPTOR_TYPE_MAX_ENUM, //descriptor type
			0, //descriptor set #
			0, //descriptor binding #
			0, //descriptor array element #
			4, //resource offset
			VK_SHADER_STAGE_FRAGMENT_BIT
		},
		{
			VK_RPI_ASSEMBLY_MAPPING_TYPE_PUSH_CONSTANT,
			VK_DESCRIPTOR_TYPE_MAX_ENUM, //descriptor type
			0, //descriptor set #
			0, //descriptor binding #
			0, //descriptor array element #
			8, //resource offset
			VK_SHADER_STAGE_FRAGMENT_BIT
		},
		{
			VK_RPI_ASSEMBLY_MAPPING_TYPE_PUSH_CONSTANT,
			VK_DESCRIPTOR_TYPE_MAX_ENUM, //descriptor type
			0, //descriptor set #
			0, //descriptor binding #
			0, //descriptor array element #
			12, //resource offset
			VK_SHADER_STAGE_FRAGMENT_BIT
		}
	};

	VkRpiShaderModuleAssemblyCreateInfoEXT shaderModuleCreateInfo = {};
	shaderModuleCreateInfo.asmStrings = blit_asm_strings;
	shaderModuleCreateInfo.mappings = blit_mappings;
	shaderModuleCreateInfo.numMappings = sizeof(blit_mappings) / sizeof(VkRpiAssemblyMappingEXT);
	shaderModuleCreateInfo.pShaderModule = blitShaderModule;

	((_device*)device)->dev->customData = (uintptr_t)&shaderModuleCreateInfo;

	//PFN_vkCreateShaderModuleFromRpiAssemblyEXT vkCreateShaderModuleFromRpiAssemblyEXT = (PFN_vkCreateShaderModuleFromRpiAssemblyEXT)vkGetInstanceProcAddr(instance, "vkCreateShaderModuleFromRpiAssemblyEXT");

	VkResult res = rpi_vkCreateShaderModuleFromRpiAssemblyEXT(((_device*)device)->dev);
	assert(blitShaderModule);
}

void createTextureToTextureShaderModule(VkDevice device, VkShaderModule* blitShaderModule)
{
	char vs_asm_code[] =
			///0x40000000 = 2.0
			///uni = 1.0
			///rb0 = 2 - 1 = 1
			"sig_small_imm ; rx0 = fsub.ws.always(b, a, uni, 0x40000000) ; nop = nop(r0, r0) ;\n"
			///set up VPM read for subsequent reads
			///0x00201a00: 0000 0000 0010 0000 0001 1010 0000 0000
			///addr: 0
			///size: 32bit
			///packed
			///horizontal
			///stride=1
			///vectors to read = 4 (TODO not exactly clear what this means...)
			"sig_load_imm ; vr_setup = load32.always(0x00401a00) ; nop = load32.always() ;\n"
			///uni = viewportXScale
			///r0 = vpm * uni
			"sig_none ; nop = nop(r0, r0, vpm_read, uni) ; r0 = fmul.always(a, b) ;\n"
			///r1 = r0 * rb0 (1)
			"sig_none ; nop = nop(r0, r0, nop, rb0) ; r1 = fmul.always(r0, b) ;\n"
			///uni = viewportYScale
			///ra0.16a = int(r1), r2 = vpm * uni
			"sig_none ; rx0.16a = ftoi.always(r1, r1, vpm_read, uni) ; r2 = fmul.always(a, b) ;\n"
			///r3 = r2 * rb0
			///r0 = vpm
			"sig_none ; r0 = or.always(a, a, vpm_read, rb0) ; r3 = fmul.always(r2, b) ;\n"
			///ra0.16b = int(r3)
			///r1 = vpm
			"sig_none ; rx0.16b = ftoi.always(r3, r3, vpm_read, nop) ; r1 = v8min.always(a, a) ;\n"
			///set up VPM write for subsequent writes
			///0x00001a00: 0000 0000 0000 0000 0001 1010 0000 0000
			///addr: 0
			///size: 32bit
			///horizontal
			///stride = 1
			"sig_load_imm ; vw_setup = load32.always.ws(0x00001a00) ; nop = load32.always() ;\n"
			///shaded vertex format for PSE
			/// Ys and Xs
			///vpm = ra0
			"sig_none ; vpm = or.always(a, a, ra0, nop) ; nop = nop(r0, r0);\n"
			/// Zs
			///uni = 0.5
			///vpm = uni
			"sig_none ; vpm = or.always(a, a, uni, nop) ; nop = nop(r0, r0);\n"
			/// 1.0 / Wc
			///vpm = rb0 (1)
			"sig_none ; vpm = or.always(b, b, nop, rb0) ; nop = nop(r0, r0);\n"
			///vpm = r0
			"sig_none ; vpm = or.always(r0, r0) ; nop = nop(r0, r0);\n"
			///vpm = r1
			"sig_none ; vpm = or.always(r1, r1) ; nop = nop(r0, r0);\n"
			///END
			"sig_end ; nop = nop(r0, r0) ; nop = nop(r0, r0) ;\n"
			"sig_none ; nop = nop(r0, r0) ; nop = nop(r0, r0) ;\n"
			"sig_none ; nop = nop(r0, r0) ; nop = nop(r0, r0) ;\n"
				"\0";

	char cs_asm_code[] =
			///uni = 1.0
			///r3 = 2.0 - uni
			"sig_small_imm ; r3 = fsub.always(b, a, uni, 0x40000000) ; nop = nop(r0, r0);\n"
			"sig_load_imm ; vr_setup = load32.always(0x00201a00) ; nop = load32.always() ;\n"
			///r2 = vpm
			"sig_none ; r2 = or.always(a, a, vpm_read, nop) ; nop = nop(r0, r0);\n"
			"sig_load_imm ; vw_setup = load32.always.ws(0x00001a00) ; nop = load32.always() ;\n"
			///shaded coordinates format for PTB
			/// write Xc
			///r1 = vpm, vpm = r2
			"sig_none ; r1 = or.always(a, a, vpm_read, nop) ; vpm = v8min.always(r2, r2);\n"
			/// write Yc
			///uni = viewportXscale
			///vpm = r1, r2 = r2 * uni
			"sig_none ; vpm = or.always(r1, r1, uni, nop) ; r2 = fmul.always(r2, a);\n"
			///uni = viewportYscale
			///r1 = r1 * uni
			"sig_none ; nop = nop(r0, r0, uni, nop) ; r1 = fmul.always(r1, a);\n"
			///r0 = r2 * r3
			"sig_none ; nop = nop(r0, r0) ; r0 = fmul.always(r2, r3);\n"
			///ra0.16a = r0, r1 = r1 * r3
			"sig_none ; rx0.16a = ftoi.always(r0, r0) ; r1 = fmul.always(r1, r3) ;\n"
			///ra0.16b = r1
			"sig_none ; rx0.16b = ftoi.always(r1, r1) ; nop = nop(r0, r0) ;\n"
			///write Zc
			///vpm = 0
			"sig_small_imm ; vpm = or.always(b, b, nop, 0) ; nop = nop(r0, r0) ;\n"
			///write Wc
			///vpm = 1.0
			"sig_small_imm ; vpm = or.always(b, b, nop, 0x3f800000) ; nop = nop(r0, r0) ;\n"
			///write Ys and Xs
			///vpm = ra0
			"sig_none ; vpm = or.always(a, a, ra0, nop) ; nop = nop(r0, r0) ;\n"
			///write Zs
			///uni = 0.5
			///vpm = uni
			"sig_none ; vpm = or.always(a, a, uni, nop) ; nop = nop(r0, r0) ;\n"
			///write 1/Wc
			///vpm = r3
			"sig_none ; vpm = or.always(r3, r3) ; nop = nop(r0, r0) ;\n"
			///END
			"sig_end ; nop = nop(r0, r0) ; nop = nop(r0, r0) ;\n"
			"sig_none ; nop = nop(r0, r0) ; nop = nop(r0, r0) ;\n"
			"sig_none ; nop = nop(r0, r0) ; nop = nop(r0, r0) ;\n"
				"\0";

	//sample texture
	char sample_fs_asm_code[] =
			///r0 = varyingX * W
			"sig_none ; nop = nop(r0, r0, pay_zw, vary) ; r0 = fmul.always(a, b) ;"
			///r2 = r0 + r5 (C)
			///r0 = varyingY * W
			"sig_none ; r2 = fadd.always(r0, r5, pay_zw, vary) ; r0 = fmul.always(a, b) ;"
			///r3 = r0 + r5 (C)
			"sig_none ; r3 = fadd.pm.always(r0, r5, nop, uni) ; r0 = v8min.always(b, b) ;"
			///write texture addresses (x, y)
			///writing tmu0_s signals that all coordinates are written
			"sig_none ; tmu0_b = or.always(r0, r0) ; nop = nop(r0, r0) ;"
			"sig_none ; tmu0_t = or.always(r3, r3) ; nop = nop(r0, r0) ;"
			"sig_none ; tmu0_s = or.always(r2, r2) ; nop = nop(r0, r0) ;"
			///suspend thread (after 2 nops) to wait for TMU request to finish
			"sig_thread_switch ; nop = nop(r0, r0) ; nop = nop(r0, r0) ;"
			"sig_none ; nop = nop(r0, r0) ; nop = nop(r0, r0) ;"
			"sig_none ; nop = nop(r0, r0) ; nop = nop(r0, r0) ;"
			///read TMU0 request result to R4
			"sig_load_tmu0 ; nop = nop(r0, r0) ; nop = nop(r0, r0) ;"
			///when thread has been awakened, MOV from R4 to R0
			"sig_none ; r0 = fmax.pm.always.8a(r4, r4) ; nop = nop(r0, r0) ;"
			"sig_none ; r1 = fmax.pm.always.8b(r4, r4) ; r0.8a = v8min.always(r0, r0) ;"
			"sig_none ; r2 = fmax.pm.always.8c(r4, r4) ; r0.8b = v8min.always(r1, r1) ;"
			"sig_none ; r3 = fmax.pm.always.8d(r4, r4) ; r0.8c = v8min.always(r2, r2) ;"
			"sig_none ; nop = nop.pm(r0, r0) ; r0.8d = v8min.always(r3, r3) ;"
			///"sig_small_imm; r0 = or.always(b, b, nop, -1) ; nop = nop(r0, r0) ;"
			"sig_none ; tlb_color_all = or.always(r0, r0) ; nop = nop(r0, r0) ;"
			"sig_end ; nop = nop(r0, r0) ; nop = nop(r0, r0) ;"
			"sig_none ; nop = nop(r0, r0) ; nop = nop(r0, r0) ;"
			"sig_unlock_score ; nop = nop(r0, r0) ; nop = nop(r0, r0) ;"
				"\0";

	char* blit_asm_strings[] =
	{
		(char*)cs_asm_code, (char*)vs_asm_code, (char*)sample_fs_asm_code, 0
	};

	VkRpiAssemblyMappingEXT blit_mappings[] = {
		//vertex shader uniforms
		{
			VK_RPI_ASSEMBLY_MAPPING_TYPE_PUSH_CONSTANT,
			VK_DESCRIPTOR_TYPE_MAX_ENUM, //descriptor type
			0, //descriptor set #
			0, //descriptor binding #
			0, //descriptor array element #
			0, //resource offset
			VK_SHADER_STAGE_VERTEX_BIT
		},
		{
			VK_RPI_ASSEMBLY_MAPPING_TYPE_PUSH_CONSTANT,
			VK_DESCRIPTOR_TYPE_MAX_ENUM, //descriptor type
			0, //descriptor set #
			0, //descriptor binding #
			0, //descriptor array element #
			4, //resource offset
			VK_SHADER_STAGE_VERTEX_BIT
		},
		{
			VK_RPI_ASSEMBLY_MAPPING_TYPE_PUSH_CONSTANT,
			VK_DESCRIPTOR_TYPE_MAX_ENUM, //descriptor type
			0, //descriptor set #
			0, //descriptor binding #
			0, //descriptor array element #
			8, //resource offset
			VK_SHADER_STAGE_VERTEX_BIT
		},
		{
			VK_RPI_ASSEMBLY_MAPPING_TYPE_PUSH_CONSTANT,
			VK_DESCRIPTOR_TYPE_MAX_ENUM, //descriptor type
			0, //descriptor set #
			0, //descriptor binding #
			0, //descriptor array element #
			12, //resource offset
			VK_SHADER_STAGE_VERTEX_BIT
		},


		//fragment shader uniforms
		{
			VK_RPI_ASSEMBLY_MAPPING_TYPE_PUSH_CONSTANT,
			VK_DESCRIPTOR_TYPE_MAX_ENUM, //descriptor type
			0, //descriptor set #
			0, //descriptor binding #
			0, //descriptor array element #
			0, //resource offset
			VK_SHADER_STAGE_FRAGMENT_BIT
		},
		{
			VK_RPI_ASSEMBLY_MAPPING_TYPE_DESCRIPTOR,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, //descriptor type
			0, //descriptor set #
			0, //descriptor binding #
			0, //descriptor array element #
			0, //resource offset
			VK_SHADER_STAGE_FRAGMENT_BIT
		},

	};

	VkRpiShaderModuleAssemblyCreateInfoEXT shaderModuleCreateInfo = {};
	shaderModuleCreateInfo.asmStrings = blit_asm_strings;
	shaderModuleCreateInfo.mappings = blit_mappings;
	shaderModuleCreateInfo.numMappings = sizeof(blit_mappings) / sizeof(VkRpiAssemblyMappingEXT);
	shaderModuleCreateInfo.pShaderModule = blitShaderModule;

	((_device*)device)->dev->customData = (uintptr_t)&shaderModuleCreateInfo;

	VkResult res = rpi_vkCreateShaderModuleFromRpiAssemblyEXT(((_device*)device)->dev);
	assert(blitShaderModule);
}

void setupEmulationResources(VkDevice device)
{
	//create resources that won't change
	_device* dev = device;

	createFullscreenQuad(device, &dev->emulFsqVertexBuffer, &dev->emulFsqVertexBufferMemory);
	createDescriptorPool(device, &dev->emulDescriptorPool);
	createDescriptorSetLayouts(device, &dev->emulBufferDsl, &dev->emulTextureDsl);
	createSampler(device, &dev->emulNearestTextureSampler, &dev->emulLinearTextureSampler);
	createBufferToTextureShaderModule(device, &dev->emulBufferToTextureShaderModule);
	createTextureToTextureShaderModule(device, &dev->emulTextureToTextureShaderModule);
}

VKAPI_ATTR void VKAPI_CALL rpi_vkCmdCopyBufferToImage(
	VkCommandBuffer                             commandBuffer,
	VkBuffer                                    srcBuffer,
	VkImage                                     dstImage,
	VkImageLayout                               dstImageLayout,
	uint32_t                                    regionCount,
	const VkBufferImageCopy*                    pRegions)
{
	_commandBuffer* cmdBuf = commandBuffer;
	_device* device = cmdBuf->dev;
	_buffer* buf = srcBuffer;
	_image* img = dstImage;

	for(uint32_t c = 0; c < regionCount; ++c)
	{
		//TODO support this
		assert(!pRegions[c].bufferRowLength);
		assert(!pRegions[c].bufferImageHeight);

		uint32_t width = pRegions[c].imageExtent.width, height = pRegions[c].imageExtent.height;

		uint32_t pixelBpp = getFormatBpp(img->format);

		VkBufferView texelBufferView;
		VkBufferViewCreateInfo bvci = {};
		bvci.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
		bvci.buffer = buf;
		bvci.format = img->format;
		bvci.offset = pRegions[c].bufferOffset;
		bvci.range = (width * height * pixelBpp) >> 3;
		rpi_vkCreateBufferView(device, &bvci, 0, &texelBufferView);

		VkDescriptorSet blitDescriptorSet;
		VkImageView textureView;
		VkRenderPass offscreenRenderPass;
		VkFramebuffer offscreenFramebuffer;
		VkPipeline blitPipeline;
		VkPipelineLayout blitPipelineLayout;

		//create blit descriptor set
		VkDescriptorSetAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = device->emulDescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &device->emulBufferDsl;
		rpi_vkAllocateDescriptorSets(device, &allocInfo, &blitDescriptorSet);

		VkWriteDescriptorSet writeDescriptorSet = {};
		writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writeDescriptorSet.dstSet = blitDescriptorSet;
		writeDescriptorSet.dstBinding = 0;
		writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
		writeDescriptorSet.pTexelBufferView = &texelBufferView;
		writeDescriptorSet.descriptorCount = 1;
		rpi_vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, 0);

		createRendertarget(device, pRegions[c].imageSubresource.baseArrayLayer, pRegions[c].imageSubresource.mipLevel, width, height, img, &textureView, &offscreenRenderPass, &offscreenFramebuffer);
		createPipeline(device, 0, 4, 5, device->emulBufferToTextureShaderModule, device->emulBufferDsl, &blitPipelineLayout, offscreenRenderPass, &blitPipeline);

		//offscreen rendering
		VkClearValue offscreenClearValues =
		{
			.color = { 1.0f, 0.0f, 1.0f, 1.0f }
		};

		VkRenderPassBeginInfo renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassInfo.renderArea.offset.x = 0;
		renderPassInfo.renderArea.offset.y = 0;
		renderPassInfo.renderArea.extent.width = width;
		renderPassInfo.renderArea.extent.height = height;
		renderPassInfo.framebuffer = offscreenFramebuffer;
		renderPassInfo.renderPass = offscreenRenderPass;
		renderPassInfo.clearValueCount = 1;
		renderPassInfo.pClearValues = &offscreenClearValues;

		rpi_vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

		rpi_vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, blitPipeline);

		VkViewport vp = {};
		vp.x = 0.0f;
		vp.y = 0.0f;
		vp.width = (float)width;
		vp.height = (float)height;
		vp.minDepth = 0.0f;
		vp.maxDepth = 1.0f;

		rpi_vkCmdSetViewport(commandBuffer, 0, 1, &vp);

		VkDeviceSize offsets = 0;
		rpi_vkCmdBindVertexBuffers(commandBuffer, 0, 1, &device->emulFsqVertexBuffer, &offsets );

		rpi_vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, blitPipelineLayout, 0, 1, &blitDescriptorSet, 0, 0);

		float Wcoeff = 1.0f; //1.0f / Wc = 2.0 - Wcoeff
		float viewportScaleX = (float)(width) * 0.5f * 16.0f;
		float viewportScaleY = -1.0f * (float)(height) * 0.5f * 16.0f;
		float Zs = 0.5f;

		uint32_t vertConstants[4];
		vertConstants[0] = *(uint32_t*)&Wcoeff;
		vertConstants[1] = *(uint32_t*)&viewportScaleX;
		vertConstants[2] = *(uint32_t*)&viewportScaleY;
		vertConstants[3] = *(uint32_t*)&Zs;

		rpi_vkCmdPushConstants(commandBuffer, blitPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(vertConstants), &vertConstants);

		float w = width;
		float bppfloat = pixelBpp;
		uint32_t size = ((width * height * pixelBpp) >> 3) - ((pixelBpp > 32 ? pixelBpp : 32) >> 3);
		uint32_t fragConstants[4];
		fragConstants[0] = *(uint32_t*)&w;
		fragConstants[1] = *(uint32_t*)&bppfloat;
		fragConstants[2] = size;
		fragConstants[3] = pRegions[c].bufferOffset;

		rpi_vkCmdPushConstants(commandBuffer, blitPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(fragConstants), &fragConstants);

		rpi_vkCmdDraw(commandBuffer, 6, 1, 0, 0);

		rpi_vkCmdEndRenderPass(commandBuffer);

		//free up resources
		rpi_vkDestroyPipelineLayout(device, blitPipelineLayout, 0);
		rpi_vkDestroyPipeline(device, blitPipeline, 0);
		rpi_vkFreeDescriptorSets(device, device->emulDescriptorPool, 1, &blitDescriptorSet);
		rpi_vkDestroyImageView(device, textureView, 0);
		rpi_vkDestroyRenderPass(device, offscreenRenderPass, 0);
		rpi_vkDestroyFramebuffer(device, offscreenFramebuffer, 0);
	}

	img->layout = dstImageLayout;
}

VKAPI_ATTR void VKAPI_CALL rpi_vkCmdBlitImage(
	VkCommandBuffer                             commandBuffer,
	VkImage                                     srcImage,
	VkImageLayout                               srcImageLayout,
	VkImage                                     dstImage,
	VkImageLayout                               dstImageLayout,
	uint32_t                                    regionCount,
	const VkImageBlit*                          pRegions,
	VkFilter                                    filter)
{
	_commandBuffer* cmdBuf = commandBuffer;
	_device* device = cmdBuf->dev;
	_image* srcImg = srcImage;
	_image* dstImg = dstImage;

	for(uint32_t c = 0; c < regionCount; ++c)
	{
		uint32_t srcWidth = pRegions[c].srcOffsets[1].x - pRegions[c].srcOffsets[0].x;
		uint32_t srcHeight = pRegions[c].srcOffsets[1].y - pRegions[c].srcOffsets[0].y;
		uint32_t dstWidth = pRegions[c].dstOffsets[1].x - pRegions[c].dstOffsets[0].x;
		uint32_t dstHeight = pRegions[c].dstOffsets[1].y - pRegions[c].dstOffsets[0].y;
		uint32_t srcMipLevel = pRegions[c].srcSubresource.mipLevel;
		uint32_t dstMipLevel = pRegions[c].dstSubresource.mipLevel;

		uint32_t srcPixelBpp = getFormatBpp(srcImg->format);
		uint32_t dstPixelBpp = getFormatBpp(dstImg->format);

		VkDescriptorSet blitDescriptorSet;
		VkImageView srcTextureView;
		VkImageView dstTextureView;
		VkRenderPass offscreenRenderPass;
		VkFramebuffer offscreenFramebuffer;
		VkPipeline blitPipeline;
		VkPipelineLayout blitPipelineLayout;

		VkSampler mipSampler;
		VkSamplerCreateInfo samplerCI = {};
		samplerCI.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerCI.magFilter = filter == VK_FILTER_LINEAR ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
		samplerCI.minFilter = filter == VK_FILTER_LINEAR ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
		samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerCI.mipLodBias = 1.0f; //disable auto lod
		samplerCI.compareOp = VK_COMPARE_OP_NEVER;
		samplerCI.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
		rpi_vkCreateSampler(device, &samplerCI, 0, &mipSampler);
		_sampler* s = mipSampler;

		VkImageViewCreateInfo view = {};
		view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		view.viewType = VK_IMAGE_VIEW_TYPE_2D;
		view.format = srcImg->format;
		view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		view.subresourceRange.baseMipLevel = srcMipLevel;
		view.subresourceRange.baseArrayLayer = 0;
		view.subresourceRange.layerCount = 1;
		view.subresourceRange.levelCount = srcImg->miplevels;
		view.image = srcImage;
		rpi_vkCreateImageView(device, &view, 0, &srcTextureView);

		//create blit descriptor set
		VkDescriptorSetAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = device->emulDescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &device->emulTextureDsl;
		rpi_vkAllocateDescriptorSets(device, &allocInfo, &blitDescriptorSet);

		VkDescriptorImageInfo imageInfo;
		imageInfo.imageView = srcTextureView;
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageInfo.sampler = mipSampler;

		VkWriteDescriptorSet writeDescriptorSet = {};
		writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writeDescriptorSet.dstSet = blitDescriptorSet;
		writeDescriptorSet.dstBinding = 0;
		writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writeDescriptorSet.pImageInfo = &imageInfo;
		writeDescriptorSet.descriptorCount = 1;
		rpi_vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, 0);

		createRendertarget(device, 0, dstMipLevel, dstWidth, dstHeight, dstImage, &dstTextureView, &offscreenRenderPass, &offscreenFramebuffer);
		createPipeline(device, 1, 4, 2, device->emulTextureToTextureShaderModule, device->emulTextureDsl, &blitPipelineLayout, offscreenRenderPass, &blitPipeline);

		//offscreen rendering
		VkClearValue offscreenClearValues =
		{
			.color = { 1.0f, 0.0f, 1.0f, 1.0f }
		};

		VkRenderPassBeginInfo renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassInfo.renderArea.offset.x = 0;
		renderPassInfo.renderArea.offset.y = 0;
		renderPassInfo.renderArea.extent.width = dstWidth;
		renderPassInfo.renderArea.extent.height = dstHeight;
		renderPassInfo.framebuffer = offscreenFramebuffer;
		renderPassInfo.renderPass = offscreenRenderPass;
		renderPassInfo.clearValueCount = 1;
		renderPassInfo.pClearValues = &offscreenClearValues;

		rpi_vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

		rpi_vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, blitPipeline);

		VkViewport vp = {};
		vp.x = 0.0f;
		vp.y = 0.0f;
		vp.width = (float)dstWidth;
		vp.height = (float)dstHeight;
		vp.minDepth = 0.0f;
		vp.maxDepth = 1.0f;

		rpi_vkCmdSetViewport(commandBuffer, 0, 1, &vp);

		VkDeviceSize offsets = 0;
		rpi_vkCmdBindVertexBuffers(commandBuffer, 0, 1, &device->emulFsqVertexBuffer, &offsets );

		rpi_vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, blitPipelineLayout, 0, 1, &blitDescriptorSet, 0, 0);

		float Wcoeff = 1.0f; //1.0f / Wc = 2.0 - Wcoeff
		float viewportScaleX = (float)(dstWidth) * 0.5f * 16.0f;
		float viewportScaleY = -1.0f * (float)(dstHeight) * 0.5f * 16.0f;
		float Zs = 0.5f;

		uint32_t vertConstants[4];
		vertConstants[0] = *(uint32_t*)&Wcoeff;
		vertConstants[1] = *(uint32_t*)&viewportScaleX;
		vertConstants[2] = *(uint32_t*)&viewportScaleY;
		vertConstants[3] = *(uint32_t*)&Zs;

		rpi_vkCmdPushConstants(commandBuffer, blitPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(vertConstants), &vertConstants);

		float mipBias = srcMipLevel;
		uint32_t fragConstants[1];
		fragConstants[0] = *(uint32_t*)&mipBias;

		rpi_vkCmdPushConstants(commandBuffer, blitPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(fragConstants), &fragConstants);

		rpi_vkCmdDraw(commandBuffer, 6, 1, 0, 0);

		rpi_vkCmdEndRenderPass(commandBuffer);

		//free up resources
		rpi_vkDestroyPipelineLayout(device, blitPipelineLayout, 0);
		rpi_vkDestroyPipeline(device, blitPipeline, 0);
		rpi_vkFreeDescriptorSets(device, device->emulDescriptorPool, 1, &blitDescriptorSet);
		rpi_vkDestroyImageView(device, srcTextureView, 0);
		rpi_vkDestroyImageView(device, dstTextureView, 0);
		rpi_vkDestroyRenderPass(device, offscreenRenderPass, 0);
		rpi_vkDestroyFramebuffer(device, offscreenFramebuffer, 0);
	}
}

VKAPI_ATTR void VKAPI_CALL rpi_vkCmdResolveImage(
	VkCommandBuffer                             commandBuffer,
	VkImage                                     srcImage,
	VkImageLayout                               srcImageLayout,
	VkImage                                     dstImage,
	VkImageLayout                               dstImageLayout,
	uint32_t                                    regionCount,
	const VkImageResolve*                       pRegions)
{
	//TODO
}

VKAPI_ATTR void VKAPI_CALL rpi_vkCmdCopyImageToBuffer(
	VkCommandBuffer                             commandBuffer,
	VkImage                                     srcImage,
	VkImageLayout                               srcImageLayout,
	VkBuffer                                    dstBuffer,
	uint32_t                                    regionCount,
	const VkBufferImageCopy*                    pRegions)
{
	//TODO
}

VKAPI_ATTR void VKAPI_CALL rpi_vkCmdCopyImage(
	VkCommandBuffer                             commandBuffer,
	VkImage                                     srcImage,
	VkImageLayout                               srcImageLayout,
	VkImage                                     dstImage,
	VkImageLayout                               dstImageLayout,
	uint32_t                                    regionCount,
	const VkImageCopy*                          pRegions)
{
	rpi_vkCmdBlitImage(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions, VK_FILTER_NEAREST);
}

VKAPI_ATTR void VKAPI_CALL rpi_vkCmdCopyBuffer(
	VkCommandBuffer                             commandBuffer,
	VkBuffer                                    srcBuffer,
	VkBuffer                                    dstBuffer,
	uint32_t                                    regionCount,
	const VkBufferCopy*                         pRegions)
{
	//TODO
}
