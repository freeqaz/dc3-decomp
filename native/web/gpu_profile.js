// DC3 web -- ?gpu_profile=<name>: run the page on a WebGPU adapter restricted
// the way another device's is, so "does it need X?" is answered by a run
// (PLAN W-29, docs/native/WEB.md "Phones: iPhone and iPad").
//
// Loaded by index.html in <head>, before the engine's JS. Without the URL
// parameter it does nothing at all (the default page is unchanged).
//
//   gpu_profile=mobile   Apple mobile (iPhone/iPad, Safari 26/27 WebGPU): the
//                        adapter shows only the optional features an Apple
//                        A-series GPU exposes -- no texture-compression-bc,
//                        no timestamp-query -- and its limits are clamped to
//                        the WebGPU spec defaults (the floor every adapter,
//                        Apple's mobile GPUs included, meets).
//   gpu_profile=bare     no optional feature at all, default limits: what any
//                        conforming WebGPU adapter is guaranteed to offer.
//   gpu_profile=record   no masking; only records what the page asks for.
//
// Every profile records into window.dc3GpuProfile:
//   adapterFeatures      what the real adapter has
//   exposedFeatures      what the page was shown
//   requestedFeatures    requiredFeatures of every requestDevice
//   requestedLimits      requiredLimits of every requestDevice
//   refused              requestDevice calls refused because they asked for a
//                        masked feature or a limit above the profile's (a real
//                        device of that kind would refuse them too)
//   deviceFeatures       the device's features as the page sees them
//   texBytes / texBytesPeak / texCount / texByFormat
//                        GPU texture memory the page allocated, from each
//                        createTexture descriptor (mips, layers, samples);
//                        destroy() subtracts. Textures the GC frees without
//                        destroy() stay counted, so this is an upper bound.
//   canvasFormat         the format the canvas context was configured with
// The masking patches GPUAdapter/GPUDevice prototype getters, which is what
// emdawnwebgpu's wgpuAdapterHasFeature/GetFeatures/GetLimits read.
(function () {
    'use strict';
    var name = new URLSearchParams(location.search).get('gpu_profile');
    if (!name) return;
    if (typeof navigator === 'undefined' || !navigator.gpu || typeof GPUAdapter === 'undefined') {
        window.dc3GpuProfile = { profile: name, error: 'no WebGPU' };
        return;
    }
    // Optional features an Apple A-series GPU's WebGPU adapter exposes in
    // Safari (WebKit HardwareCapabilities: Apple GPU families; BC is a Mac
    // feature). Kept to what the page can be asked about; anything not
    // listed is masked.
    var APPLE_MOBILE = ['depth-clip-control', 'depth32float-stencil8', 'texture-compression-etc2',
        'texture-compression-astc', 'indirect-first-instance', 'shader-f16',
        'rg11b10ufloat-renderable', 'bgra8unorm-storage', 'float32-filterable'];
    // WebGPU spec default limits (https://www.w3.org/TR/webgpu/#limits).
    var DEFAULT_LIMITS = {
        maxTextureDimension1D: 8192, maxTextureDimension2D: 8192, maxTextureDimension3D: 2048,
        maxTextureArrayLayers: 256, maxBindGroups: 4, maxBindGroupsPlusVertexBuffers: 24,
        maxBindingsPerBindGroup: 1000, maxDynamicUniformBuffersPerPipelineLayout: 8,
        maxDynamicStorageBuffersPerPipelineLayout: 4, maxSampledTexturesPerShaderStage: 16,
        maxSamplersPerShaderStage: 16, maxStorageBuffersPerShaderStage: 8,
        maxStorageTexturesPerShaderStage: 4, maxUniformBuffersPerShaderStage: 12,
        maxUniformBufferBindingSize: 65536, maxStorageBufferBindingSize: 134217728,
        minUniformBufferOffsetAlignment: 256, minStorageBufferOffsetAlignment: 256,
        maxVertexBuffers: 8, maxBufferSize: 268435456, maxVertexAttributes: 16,
        maxVertexBufferArrayStride: 2048, maxInterStageShaderVariables: 16,
        maxColorAttachments: 8, maxColorAttachmentBytesPerSample: 32,
        maxComputeWorkgroupStorageSize: 16384, maxComputeInvocationsPerWorkgroup: 256,
        maxComputeWorkgroupSizeX: 256, maxComputeWorkgroupSizeY: 256, maxComputeWorkgroupSizeZ: 64,
        maxComputeWorkgroupsPerDimension: 65535,
    };
    var masking = name === 'mobile' || name === 'bare';
    if (!masking && name !== 'record') {
        console.warn('[gpu_profile] unknown profile "' + name + '" (mobile | bare | record); recording only');
    }
    var allow = name === 'mobile' ? APPLE_MOBILE : name === 'bare' ? [] : null;
    var P = window.dc3GpuProfile = {
        profile: name, adapterFeatures: null, exposedFeatures: null, requestedFeatures: [],
        requestedLimits: [], refused: [], deviceFeatures: null, canvasFormat: null,
        texBytes: 0, texBytesPeak: 0, texCount: 0, texByFormat: {},
    };

    function desc(proto, key) { return Object.getOwnPropertyDescriptor(proto, key); }
    var adapterFeatures = desc(GPUAdapter.prototype, 'features');
    var adapterLimits = desc(GPUAdapter.prototype, 'limits');
    var deviceFeatures = desc(GPUDevice.prototype, 'features');
    var deviceLimits = desc(GPUDevice.prototype, 'limits');
    var LIMIT_KEYS = Object.keys(DEFAULT_LIMITS);
    function isAlignment(k) { return /^min/.test(k); }
    function maskSet(real) {
        var out = new Set();
        real.forEach(function (f) { if (!allow || allow.indexOf(f) >= 0) out.add(f); });
        return out;
    }
    // Clamp a real limits object to the defaults (alignments: the larger).
    function maskLimits(real) {
        var out = {};
        for (var k in real) out[k] = real[k];          // GPUSupportedLimits: own getters on the prototype
        LIMIT_KEYS.forEach(function (k) {
            var v = real[k];
            if (v === undefined) return;
            out[k] = isAlignment(k) ? Math.max(v, DEFAULT_LIMITS[k]) : Math.min(v, DEFAULT_LIMITS[k]);
        });
        return out;
    }
    if (masking) {
        Object.defineProperty(GPUAdapter.prototype, 'features', {
            configurable: true, get: function () {
                var real = adapterFeatures.get.call(this);
                if (!P.adapterFeatures) P.adapterFeatures = Array.from(real).sort();
                var m = maskSet(real);
                P.exposedFeatures = Array.from(m).sort();
                return m;
            },
        });
        Object.defineProperty(GPUAdapter.prototype, 'limits', {
            configurable: true, get: function () { return maskLimits(adapterLimits.get.call(this)); },
        });
        Object.defineProperty(GPUDevice.prototype, 'features', {
            configurable: true, get: function () { return maskSet(deviceFeatures.get.call(this)); },
        });
        Object.defineProperty(GPUDevice.prototype, 'limits', {
            configurable: true, get: function () { return maskLimits(deviceLimits.get.call(this)); },
        });
        // Apple's preferred canvas format.
        navigator.gpu.getPreferredCanvasFormat = function () { return 'bgra8unorm'; };
    }
    var requestDevice = GPUAdapter.prototype.requestDevice;
    GPUAdapter.prototype.requestDevice = function (d) {
        var feats = (d && d.requiredFeatures) ? Array.from(d.requiredFeatures) : [];
        var lims = (d && d.requiredLimits) ? Object.assign({}, d.requiredLimits) : {};
        P.requestedFeatures.push(feats);
        P.requestedLimits.push(lims);
        if (!P.adapterFeatures) P.adapterFeatures = Array.from(adapterFeatures.get.call(this)).sort();
        if (masking) {
            var bad = feats.filter(function (f) { return allow.indexOf(f) < 0; });
            var over = Object.keys(lims).filter(function (k) {
                if (!(k in DEFAULT_LIMITS) || lims[k] === undefined) return false;
                return isAlignment(k) ? lims[k] < DEFAULT_LIMITS[k] : lims[k] > DEFAULT_LIMITS[k];
            });
            if (bad.length || over.length) {
                var why = (bad.length ? 'features not in profile ' + name + ': ' + bad.join(',') : '') +
                          (over.length ? (bad.length ? '; ' : '') + 'limits above profile: ' + over.join(',') : '');
                P.refused.push(why);
                console.error('[gpu_profile] requestDevice refused: ' + why);
                return Promise.reject(bad.length ? new TypeError(why) : new DOMException(why, 'OperationError'));
            }
        }
        return requestDevice.call(this, d).then(function (dev) {
            P.deviceFeatures = Array.from(dev.features).sort();
            console.log('[gpu_profile] ' + name + ': adapter [' + (P.adapterFeatures || []).join(' ') +
                '] -> device [' + P.deviceFeatures.join(' ') + '], requested [' + feats.join(' ') + ']');
            return dev;
        });
    };

    // GPU texture memory, from the descriptors.
    var BPT = {   // bytes per texel (block formats: per 4x4 block / 16)
        'r8unorm': 1, 'rg8unorm': 2, 'rgba8unorm': 4, 'rgba8unorm-srgb': 4, 'bgra8unorm': 4,
        'bgra8unorm-srgb': 4, 'rgba16float': 8, 'rgba32float': 16, 'r32float': 4, 'rg16float': 4,
        'rgb10a2unorm': 4, 'rg11b10ufloat': 4, 'depth16unorm': 2, 'depth24plus': 4,
        'depth24plus-stencil8': 5, 'depth32float': 4, 'depth32float-stencil8': 5, 'stencil8': 1,
        'bc1-rgba-unorm': 0.5, 'bc1-rgba-unorm-srgb': 0.5, 'bc2-rgba-unorm': 1, 'bc2-rgba-unorm-srgb': 1,
        'bc3-rgba-unorm': 1, 'bc3-rgba-unorm-srgb': 1, 'bc4-r-unorm': 0.5, 'bc5-rg-unorm': 1,
    };
    function texBytes(d) {
        var s = d.size, w, h, layers;
        if (Array.isArray(s) || (s && typeof s[Symbol.iterator] === 'function' && !('width' in s))) {
            var a = Array.from(s); w = a[0] || 1; h = a[1] || 1; layers = a[2] || 1;
        } else { w = s.width || 1; h = s.height || 1; layers = s.depthOrArrayLayers || 1; }
        var bpt = BPT[d.format] !== undefined ? BPT[d.format] : 4;
        var block = /^bc|etc2|astc/.test(d.format);
        var total = 0, mips = d.mipLevelCount || 1;
        for (var i = 0; i < mips; i++) {
            var mw = Math.max(1, w >> i), mh = Math.max(1, h >> i);
            if (block) { mw = Math.ceil(mw / 4) * 4; mh = Math.ceil(mh / 4) * 4; }
            total += mw * mh * bpt;
        }
        return Math.round(total * layers * (d.sampleCount || 1));
    }
    var sizes = new WeakMap();
    var createTexture = GPUDevice.prototype.createTexture;
    GPUDevice.prototype.createTexture = function (d) {
        var t = createTexture.call(this, d);
        try {
            var b = texBytes(d);
            sizes.set(t, [b, d.format]);
            P.texBytes += b; P.texCount++;
            if (P.texBytes > P.texBytesPeak) P.texBytesPeak = P.texBytes;
            var f = P.texByFormat[d.format] || (P.texByFormat[d.format] = { count: 0, bytes: 0 });
            f.count++; f.bytes += b;
        } catch (e) { /* accounting only */ }
        return t;
    };
    var destroy = GPUTexture.prototype.destroy;
    GPUTexture.prototype.destroy = function () {
        var r = sizes.get(this);
        if (r) {
            sizes.delete(this);
            P.texBytes -= r[0]; P.texCount--;
            var f = P.texByFormat[r[1]];
            if (f) { f.count--; f.bytes -= r[0]; }
        }
        return destroy.call(this);
    };
    if (typeof GPUCanvasContext !== 'undefined') {
        var configure = GPUCanvasContext.prototype.configure;
        GPUCanvasContext.prototype.configure = function (c) {
            P.canvasFormat = c && c.format;
            return configure.call(this, c);
        };
    }
    console.log('[gpu_profile] ' + name + (masking ? ' (masking adapter features/limits)' : ' (recording only)'));
})();
