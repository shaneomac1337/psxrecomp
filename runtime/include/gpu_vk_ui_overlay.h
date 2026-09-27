#ifndef PSX_GPU_VK_UI_OVERLAY_H
#define PSX_GPU_VK_UI_OVERLAY_H

/* Host UI overlay hook for the Vulkan present path.
 *
 * A UI toolkit (the in-game menu uses Dear ImGui's Vulkan backend) draws into
 * the swapchain image after the game frame and the OSD, inside a render pass
 * the renderer owns: one color attachment, LOAD/STORE, entered and left in
 * TRANSFER_DST_OPTIMAL so the present barrier stays unchanged. The renderer
 * only calls the record hook while one is installed. */

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VkUiOverlayEnv {
    PFN_vkGetInstanceProcAddr get_instance_proc_addr;
    uint32_t         api_version;
    VkInstance       instance;
    VkPhysicalDevice physical_device;
    VkDevice         device;
    uint32_t         queue_family;
    VkQueue          queue;
    VkRenderPass     render_pass;    /* compatible with the overlay pass */
    uint32_t         image_count;    /* swapchain images */
} VkUiOverlayEnv;

/* Records draw commands into cb inside the overlay render pass. */
typedef void (*VkUiOverlayRecordFn)(VkCommandBuffer cb);

/* Fill the device handles a UI backend needs. Returns 0 when the Vulkan
 * present path is unavailable (no context, or the present pass failed). */
int  vk_renderer_ui_env(VkUiOverlayEnv *out);
/* Install (or with NULL remove) the overlay record hook. */
void vk_renderer_set_ui_overlay(VkUiOverlayRecordFn fn);

#ifdef __cplusplus
}
#endif

#endif /* PSX_GPU_VK_UI_OVERLAY_H */
