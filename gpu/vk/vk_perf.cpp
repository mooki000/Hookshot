// (hookshot) Renderer-owned counters cross to the sampler as one small snapshot, never as racing reads of globals.
#include "vk_perf.h"
#include "perf_sampler.h"
#include "vk_internal.h"
#include "vk_draw.h"
#include "vk_texture.h"
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>
#ifndef HOOKSHOT_VERSION
#define HOOKSHOT_VERSION "unknown"
#endif

namespace mkwvk {
std::atomic<bool> perf_collecting{false};
namespace {
namespace pm = perf_monitor;
std::unique_ptr<pm::Sampler> sampler;
pm::RenderSample latest; // Only the replay thread writes this; the worker gets publish()'s locked copy.
void report(const char* text) { logf("%s", text); }
}
void perf_start() {
    if (sampler) return;
    std::vector<std::string> errors;
    pm::Config config = pm::configuration(std::getenv, errors);
    for (const std::string& error : errors) logf("vk: perf: %s", error.c_str());
    if (!config.enabled()) return; // In particular: no device queries, allocation, thread or optional library while off.
    pm::Device gpu;
    gpu.vendor_id = dev.props.vendorID;
    VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &id};
    vkGetPhysicalDeviceProperties2(dev.physical, &properties);
    bool have_uuid = false;
    for (uint8_t c : id.deviceUUID) have_uuid |= c != 0;
    if (have_uuid) {
        char text[64];
        const auto* u = id.deviceUUID;
        std::snprintf(text, sizeof text, "GPU-%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                      u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7], u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15]);
        gpu.uuid = text;
    }
    uint32_t count = 0;
    if (vkEnumerateDeviceExtensionProperties(dev.physical, nullptr, &count, nullptr) == VK_SUCCESS) {
        std::vector<VkExtensionProperties> extensions(count);
        if (vkEnumerateDeviceExtensionProperties(dev.physical, nullptr, &count, extensions.data()) == VK_SUCCESS) {
            for (const auto& extension : extensions) if (!std::strcmp(extension.extensionName, VK_EXT_PCI_BUS_INFO_EXTENSION_NAME)) {
                VkPhysicalDevicePCIBusInfoPropertiesEXT pci{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT};
                properties.pNext = &pci;
                vkGetPhysicalDeviceProperties2(dev.physical, &properties);
                char address[32];
                std::snprintf(address, sizeof address, "%04x:%02x:%02x.%x", pci.pciDomain, pci.pciBus, pci.pciDevice, pci.pciFunction);
                gpu.pci_address = address;
                break;
            }
        }
    }
    sampler = std::make_unique<pm::Sampler>(std::move(config), std::move(gpu), dev.offscreen ? "headless-renderer" : "swapchain",
                                           HOOKSHOT_VERSION, report, perf_collecting);
    sampler->start();
}
void perf_stop() { if (sampler) sampler->stop(); }
void perf_game_present() { sampler->game_present(); }
void perf_present(int64_t ns, double ms, int64_t frame, double time_s) { sampler->frame(ns, ms, frame, time_s); }
void perf_texture_budget(uint64_t used, uint64_t allowed, uint64_t textures, uint64_t budget, bool known) {
    // Reuse the budget pass's existing driver query. An allocation-only fallback is not driver usage/budget.
    latest.values[pm::kVramUsed] = known ? pm::Value::of(static_cast<double>(used)) : pm::Value{};
    latest.values[pm::kVramBudget] = known && allowed ? pm::Value::of(static_cast<double>(allowed)) : pm::Value{};
    latest.values[pm::kTextureUsed] = pm::Value::of(static_cast<double>(textures));
    latest.values[pm::kTextureBudget] = pm::Value::of(static_cast<double>(budget));
}
void perf_replay_present(int64_t present, int64_t gpu_ns, int64_t wait_ns, bool gpu_timing) {
    sampler->replay_present(present);
    if (!sampler->needs_snapshot()) return;
    latest.present = present;
    const pm::Mask fields = sampler->fields();
    if (fields & (pm::bit(pm::kPipelines) | pm::bit(pm::kLeftOut))) {
        int64_t draws = 0, skipped = 0, dispatches = 0, pipelines = 0, left = 0, waits = 0;
        double waited = 0;
        draw_stats(&draws, &skipped, &dispatches, &pipelines);
        draw_stats3(&left, &waits, &waited);
        latest.values[pm::kPipelines] = pm::Value::of(static_cast<double>(pipelines));
        latest.values[pm::kLeftOut] = pm::Value::of(static_cast<double>(left));
    }
    if (fields & pm::bit(pm::kUploadBytes)) {
        int64_t uploads = 0, astc = 0, bytes = 0;
        texture_stats(&uploads, &astc, &bytes);
        latest.values[pm::kUploadBytes] = pm::Value::of(static_cast<double>(bytes));
    }
    latest.values[pm::kGpuMs] = gpu_timing ? pm::Value::of(gpu_ns / 1e6) : pm::Value{};
    latest.values[pm::kGpuWaitMs] = pm::Value::of(wait_ns / 1e6);
    sampler->publish(latest);
}
} // namespace mkwvk
