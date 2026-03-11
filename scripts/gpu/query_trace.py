#!/usr/bin/env python3
"""Query GFXReconstruct JSON traces efficiently.

Stream-parses large pretty-printed JSON arrays from gfxrecon-convert output.
Filters by API call name, index range, and jq-style field paths.

Usage:
    # Find all pipeline creation calls
    query_trace.py /tmp/trace.jsonl --call CreateGraphicsPipelines

    # Show blend state for all pipelines
    query_trace.py /tmp/trace.jsonl --call CreateGraphicsPipelines --field pColorBlendState

    # Find draw calls in index range
    query_trace.py /tmp/trace.jsonl --call vkCmdDrawIndexed --range 5000-6000

    # Show all render pass begins
    query_trace.py /tmp/trace.jsonl --call BeginRendering

    # List debug labels
    query_trace.py /tmp/trace.jsonl --call SetDebugUtilsObjectNameEXT --field pObjectName

    # Summary: count API calls
    query_trace.py /tmp/trace.jsonl --summary

    # First N matches
    query_trace.py /tmp/trace.jsonl --call vkCmdDrawIndexed --limit 5

    # Show specific entry by index
    query_trace.py /tmp/trace.jsonl --index 2007

    # Compact output (one line per match)
    query_trace.py /tmp/trace.jsonl --call vkCmdDrawIndexed --compact

    # Search for any text in entries
    query_trace.py /tmp/trace.jsonl --grep "SRC_ALPHA"

    # Pipe-friendly: just the JSON, no decoration
    query_trace.py /tmp/trace.jsonl --call vkCmdDrawIndexed --raw | jq .
"""

import argparse
import json
import re
import sys
from collections import defaultdict
from pathlib import Path


def iter_entries(filepath):
    """Stream-parse a pretty-printed JSON array, yielding (raw_text, parsed_dict) tuples.

    Uses brace/bracket depth tracking to find entry boundaries without
    loading the entire file into memory.
    """
    with open(filepath) as f:
        first_line = f.readline().strip()
        if first_line != '[':
            # Might be single-line JSON array or JSONL
            if first_line.startswith('[{'):
                # Single-line JSON array — fall back to json.loads
                f.seek(0)
                data = json.loads(f.read())
                for entry in data:
                    yield json.dumps(entry), entry
                return
            elif first_line.startswith('{'):
                # True JSONL format
                f.seek(0)
                for line in f:
                    line = line.strip()
                    if line:
                        try:
                            yield line, json.loads(line)
                        except json.JSONDecodeError:
                            continue
                return
            else:
                raise ValueError(f"Unexpected format, starts with: {first_line[:50]}")

        buf = []
        depth = 0

        for line in f:
            buf.append(line)
            depth += line.count('{') + line.count('[')
            depth -= line.count('}') + line.count(']')

            if depth <= 0 and buf:
                text = ''.join(buf)
                # Strip trailing comma between array entries
                text_stripped = text.rstrip().rstrip(',')
                buf = []
                depth = 0
                try:
                    entry = json.loads(text_stripped)
                    yield text_stripped, entry
                except json.JSONDecodeError:
                    continue


def get_call_name(entry):
    """Extract the Vulkan API call name from an entry."""
    func = entry.get('function', {})
    return func.get('name', '')


def get_index(entry):
    """Extract the index from an entry."""
    return entry.get('index')


def extract_field(obj, path):
    """Extract a field by dot-separated path. Descends into nested dicts and lists."""
    parts = path.split('.')
    current = obj
    for part in parts:
        if isinstance(current, dict):
            if part in current:
                current = current[part]
            else:
                # Search recursively for the key
                found = _find_key(current, part)
                if found is not None:
                    current = found
                else:
                    return None
        elif isinstance(current, list):
            try:
                idx = int(part)
                current = current[idx]
            except (ValueError, IndexError):
                # Apply to all list elements
                results = [extract_field(item, part) for item in current]
                return [r for r in results if r is not None]
        else:
            return None
    return current


def _find_key(d, key):
    """Recursively search for a key in nested dicts."""
    if key in d:
        return d[key]
    for v in d.values():
        if isinstance(v, dict):
            result = _find_key(v, key)
            if result is not None:
                return result
        elif isinstance(v, list):
            for item in v:
                if isinstance(item, dict):
                    result = _find_key(item, key)
                    if result is not None:
                        return result
    return None


def format_compact(entry):
    """One-line compact representation of an entry."""
    idx = get_index(entry)
    name = get_call_name(entry)
    func = entry.get('function', {})
    ret = func.get('return', '')
    args = func.get('args', {})

    # Build compact arg summary
    arg_parts = []
    for k, v in args.items():
        if isinstance(v, (dict, list)):
            if isinstance(v, list):
                arg_parts.append(f"{k}=[{len(v)} items]")
            else:
                arg_parts.append(f"{k}={{...}}")
        else:
            arg_parts.append(f"{k}={v}")

    args_str = ', '.join(arg_parts[:6])
    if len(arg_parts) > 6:
        args_str += f', ... +{len(arg_parts) - 6} more'

    prefix = f"[{idx}]" if idx is not None else "[-]"
    ret_str = f" -> {ret}" if ret else ""
    return f"{prefix} {name}({args_str}){ret_str}"


def cmd_query(args):
    """Main query command."""
    matches = 0
    range_min, range_max = None, None
    if args.range:
        parts = args.range.split('-')
        range_min = int(parts[0])
        range_max = int(parts[1]) if len(parts) > 1 else range_min

    target_indices = set()
    if args.index is not None:
        for part in args.index.split(','):
            if '-' in part:
                lo, hi = part.split('-')
                target_indices.update(range(int(lo), int(hi) + 1))
            else:
                target_indices.add(int(part))

    for raw, entry in iter_entries(args.file):
        idx = get_index(entry)
        name = get_call_name(entry)

        # Filter by index
        if target_indices and (idx is None or idx not in target_indices):
            continue

        # Filter by range
        if range_min is not None:
            if idx is None or idx < range_min:
                continue
            if idx > range_max:
                break  # Past range, done

        # Filter by call name
        if args.call and args.call.lower() not in name.lower():
            continue

        # Filter by grep
        if args.grep and args.grep not in raw:
            continue

        # Match found
        matches += 1

        if args.field:
            # Extract specific field
            value = extract_field(entry, args.field)
            if value is not None:
                if args.raw:
                    print(json.dumps(value))
                else:
                    prefix = f"[{idx}] {name}" if name else f"[{idx}]"
                    print(f"--- {prefix} ---")
                    if isinstance(value, (dict, list)):
                        print(json.dumps(value, indent=2))
                    else:
                        print(value)
        elif args.compact:
            print(format_compact(entry))
        elif args.raw:
            print(json.dumps(entry))
        else:
            print(f"--- [{idx}] {name} ---")
            print(json.dumps(entry, indent=2))

        if args.limit and matches >= args.limit:
            break

    if not args.raw:
        print(f"\n=== {matches} match(es) ===", file=sys.stderr)


def cmd_summary(args):
    """Count API calls by name."""
    counts = defaultdict(int)
    total = 0

    for _, entry in iter_entries(args.file):
        name = get_call_name(entry)
        if name:
            counts[name] += 1
            total += 1

    print(f"{'Count':>8}  API Call")
    print(f"{'-----':>8}  --------")
    for name, count in sorted(counts.items(), key=lambda x: -x[1]):
        print(f"{count:8d}  {name}")
    print(f"\n{'Total':>8}: {total} API calls, {len(counts)} unique")


def cmd_pipelines(args):
    """Show pipeline creation details — blend state, vertex input, etc."""
    for _, entry in iter_entries(args.file):
        name = get_call_name(entry)
        if 'CreateGraphicsPipelines' not in name:
            continue

        idx = get_index(entry)
        func = entry.get('function', {})
        func_args = func.get('args', {})
        create_infos = func_args.get('pCreateInfos', [])
        pipeline = func_args.get('pPipelines', [])
        pipeline_handle = pipeline[0] if pipeline else '?'

        for i, info in enumerate(create_infos):
            print(f"=== Pipeline [{idx}] handle={pipeline_handle} ===")

            # Stages
            stages = info.get('pStages', [])
            stage_names = [s.get('stage', '').replace('VK_SHADER_STAGE_', '').replace('_BIT', '')
                          for s in stages]
            modules = [s.get('module', '?') for s in stages]
            print(f"  Stages: {', '.join(stage_names)}")
            print(f"  Shader modules: {modules}")

            # Vertex input
            vi = info.get('pVertexInputState', {})
            if vi:
                bindings = vi.get('pVertexBindingDescriptions', [])
                attrs = vi.get('pVertexAttributeDescriptions', [])
                for b in bindings:
                    print(f"  Vertex binding {b.get('binding')}: stride={b.get('stride')}")
                for a in attrs:
                    fmt = a.get('format', '').replace('VK_FORMAT_', '')
                    print(f"    location={a.get('location')} offset={a.get('offset')} {fmt}")

            # Input assembly
            ia = info.get('pInputAssemblyState', {})
            if ia:
                topo = ia.get('topology', '').replace('VK_PRIMITIVE_TOPOLOGY_', '')
                print(f"  Topology: {topo}")

            # Rasterization
            rs = info.get('pRasterizationState', {})
            if rs:
                cull = rs.get('cullMode', '').replace('VK_CULL_MODE_', '')
                front = rs.get('frontFace', '').replace('VK_FRONT_FACE_', '')
                print(f"  Raster: cull={cull} front={front}")

            # Depth/stencil
            ds = info.get('pDepthStencilState', {})
            if ds:
                de = ds.get('depthTestEnable', False)
                dw = ds.get('depthWriteEnable', False)
                dop = ds.get('depthCompareOp', '').replace('VK_COMPARE_OP_', '')
                se = ds.get('stencilTestEnable', False)
                print(f"  Depth: test={de} write={dw} op={dop} stencil={se}")

            # COLOR BLEND (the key one for debugging)
            cb = info.get('pColorBlendState', {})
            if cb:
                logic_en = cb.get('logicOpEnable', False)
                attachments = cb.get('pAttachments') or []
                print(f"  Blend: logicOp={logic_en}, {len(attachments)} attachment(s)")
                for j, att in enumerate(attachments):
                    blend_en = att.get('blendEnable', False)
                    src_c = att.get('srcColorBlendFactor', '').replace('VK_BLEND_FACTOR_', '')
                    dst_c = att.get('dstColorBlendFactor', '').replace('VK_BLEND_FACTOR_', '')
                    op_c = att.get('colorBlendOp', '').replace('VK_BLEND_OP_', '')
                    src_a = att.get('srcAlphaBlendFactor', '').replace('VK_BLEND_FACTOR_', '')
                    dst_a = att.get('dstAlphaBlendFactor', '').replace('VK_BLEND_FACTOR_', '')
                    op_a = att.get('alphaBlendOp', '').replace('VK_BLEND_OP_', '')
                    mask = att.get('colorWriteMask', '')
                    if blend_en:
                        print(f"    [{j}] BLEND ON: color={src_c} {op_c} {dst_c} | alpha={src_a} {op_a} {dst_a} | mask={mask}")
                    else:
                        print(f"    [{j}] BLEND OFF | mask={mask}")

            # Dynamic state
            dyn = info.get('pDynamicState', {})
            if dyn:
                states = dyn.get('pDynamicStates', [])
                short = [s.replace('VK_DYNAMIC_STATE_', '') for s in states]
                print(f"  Dynamic: {', '.join(short)}")

            # Render pass
            rp = info.get('renderPass')
            sp = info.get('subpass')
            if rp:
                print(f"  RenderPass: {rp}, subpass={sp}")

            print()


def cmd_draws(args):
    """Show draw calls with their context (bound pipeline, descriptors)."""
    current_pipeline = None
    current_render = None

    for _, entry in iter_entries(args.file):
        idx = get_index(entry)
        name = get_call_name(entry)

        if 'BeginRendering' in name:
            func = entry.get('function', {})
            func_args = func.get('args', {})
            ri = func_args.get('pRenderingInfo', {})
            color_atts = ri.get('pColorAttachments') or []
            render_area = ri.get('renderArea') or {}
            extent = render_area.get('extent', {})
            current_render = {
                'index': idx,
                'width': extent.get('width'),
                'height': extent.get('height'),
                'color_attachments': len(color_atts),
            }

        elif 'BindPipeline' in name:
            func = entry.get('function', {})
            func_args = func.get('args', {})
            current_pipeline = func_args.get('pipeline')

        elif 'DrawIndexed' in name or (name == 'vkCmdDraw' and 'Indexed' not in name):
            func = entry.get('function', {})
            func_args = func.get('args', {})

            index_count = func_args.get('indexCount', func_args.get('vertexCount', '?'))
            instance_count = func_args.get('instanceCount', 1)
            first = func_args.get('firstIndex', func_args.get('firstVertex', 0))

            render_str = ""
            if current_render:
                render_str = f" render={current_render['width']}x{current_render['height']}"

            print(f"[{idx}] {name}: {index_count} indices, {instance_count} instances, first={first}, pipeline={current_pipeline}{render_str}")

        elif 'EndRendering' in name:
            current_render = None


def cmd_labels(args):
    """Show debug labels/object names."""
    for _, entry in iter_entries(args.file):
        name = get_call_name(entry)
        if 'SetDebugUtilsObjectName' not in name:
            continue

        idx = get_index(entry)
        func = entry.get('function', {})
        func_args = func.get('args', {})
        name_info = func_args.get('pNameInfo', {})
        obj_type = name_info.get('objectType', '').replace('VK_OBJECT_TYPE_', '')
        obj_handle = name_info.get('objectHandle')
        obj_name = name_info.get('pObjectName', '')

        if args.grep and args.grep.lower() not in obj_name.lower():
            continue

        print(f"[{idx}] {obj_type} {obj_handle} = \"{obj_name}\"")


def main():
    parser = argparse.ArgumentParser(
        description='Query GFXReconstruct JSON traces',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    parser.add_argument('file', help='Path to converted .jsonl file')

    # Mode selection
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--summary', action='store_true', help='Count API calls by name')
    mode.add_argument('--pipelines', action='store_true', help='Show pipeline creation details')
    mode.add_argument('--draws', action='store_true', help='Show draw calls with context')
    mode.add_argument('--labels', action='store_true', help='Show debug labels/object names')

    # Filtering
    parser.add_argument('--call', '-c', help='Filter by API call name (substring match)')
    parser.add_argument('--index', '-i', help='Show specific index(es): 2007 or 2007,2008 or 2007-2010')
    parser.add_argument('--range', '-r', help='Filter by index range: MIN-MAX')
    parser.add_argument('--grep', '-g', help='Filter by text in raw JSON')
    parser.add_argument('--limit', '-n', type=int, help='Max results')

    # Output
    parser.add_argument('--field', '-f', help='Extract specific field (dot path or recursive search)')
    parser.add_argument('--compact', action='store_true', help='One-line output per match')
    parser.add_argument('--raw', action='store_true', help='Raw JSON output (pipe-friendly)')

    args = parser.parse_args()

    if args.summary:
        cmd_summary(args)
    elif args.pipelines:
        cmd_pipelines(args)
    elif args.draws:
        cmd_draws(args)
    elif args.labels:
        cmd_labels(args)
    else:
        if not args.call and not args.index and not args.grep and not args.range:
            parser.error("Specify --call, --index, --grep, --range, or a mode (--summary/--pipelines/--draws/--labels)")
        cmd_query(args)


if __name__ == '__main__':
    main()
