/**
 * Shared Chrome/Playwright launch configuration for DC3 web tests.
 *
 * Configures headless Chrome with proper WebGPU support on Linux:
 * - Uses --headless=new (real browser, not lightweight shell)
 * - Forces Vulkan ANGLE backend for WebGPU
 * - Disables Vulkan surface (no swapchain needed headless)
 * - Enables unsafe-webgpu for non-HTTPS origins
 */

const { chromium } = require('playwright');

/**
 * Chrome args for headless WebGPU on Linux.
 * Works both with and without xvfb — --headless=new provides a real
 * compositor that doesn't require an X11 display for GPU rendering.
 */
const WEBGPU_ARGS = [
    '--no-sandbox',
    '--headless=new',

    // WebGPU on Linux via Vulkan ANGLE
    '--use-angle=vulkan',
    '--enable-features=Vulkan,VulkanFromANGLE,DefaultANGLEVulkan',
    '--disable-vulkan-surface',
    '--enable-unsafe-webgpu',

    // Override GPU blocklist (some headless environments report blocklisted GPUs)
    '--ignore-gpu-blocklist',

    // Reduce startup noise
    '--disable-extensions',
    '--disable-background-networking',
    '--disable-default-apps',
    '--disable-sync',
    '--mute-audio',
];

/**
 * Launch Chrome with WebGPU support.
 * Always headless (--headless=new handles GPU rendering).
 */
async function launchBrowser(extraArgs = []) {
    return chromium.launch({
        headless: true,
        args: [...WEBGPU_ARGS, ...extraArgs],
    });
}

/**
 * Wait for the DC3 engine to signal that WebGPU frames are being rendered.
 * The engine sets window.__webgpuReady = true after 3 frames in BOOT_RUNNING.
 *
 * @param {import('playwright').Page} page
 * @param {number} timeoutMs - Max time to wait (default 120s — engine init is slow)
 */
async function waitForWebGPUReady(page, timeoutMs = 120000) {
    await page.waitForFunction(
        () => window.__webgpuReady === true,
        null,
        { timeout: timeoutMs }
    );
}

/**
 * Take a screenshot after ensuring WebGPU content is composited.
 * Waits two animation frames after __webgpuReady to let the browser
 * composite the latest GPU output onto the page.
 *
 * @param {import('playwright').Page} page
 * @param {string} path - Output file path
 */
async function screenshotReady(page, path) {
    // Give the compositor a couple frames to present GPU content
    await page.evaluate(() => new Promise(r => requestAnimationFrame(() => requestAnimationFrame(r))));
    await page.screenshot({ path });
}

module.exports = {
    WEBGPU_ARGS,
    launchBrowser,
    waitForWebGPUReady,
    screenshotReady,
};
