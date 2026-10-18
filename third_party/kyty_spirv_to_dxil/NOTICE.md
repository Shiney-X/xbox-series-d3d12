<!-- SPDX-FileCopyrightText: 2026 KytyPS5 contributors -->
<!-- SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors -->
<!-- SPDX-License-Identifier: GPL-2.0-only -->
# KytyPS5 / Mesa shader translation adaptation

Source: https://github.com/Shiney-X/KytyPS5

Audited donor revision: `cd0c360aa7b432645b915c19fd1ceed394378b75`.

`spirv_to_dxil.patch` is copied unchanged from
`3rdparty/spirv_to_dxil/spirv_to_dxil.patch` of that revision.
SHA-256: `326b1367c84959a363feeec28c4d1c3adcc6b935ff891dfde1ce1d87b44b29e1`.
It applies to Mesa `e24dc5bd1e7fe6101bdc866fb16a15a8fcae1aae`.
The Mesa source itself is fetched separately; it is not vendored in this folder.
Mesa's own notices/license remain in the source and packaged license.rst.

Adapted donor files:

- `src/graphics/host_gpu/d3d12/dxilCompiler.{h,cpp}`: translation configuration,
  stage linking, shader-model/validator negotiation and DXIL signing, adapted
  into `platform/xbox_uwp/shaders/mesa_dxil_bridge.{h,cpp}`.
- `.github/workflows/xbox.yml`: pinned Mesa build recipe, adapted into
  `scripts/windows/Build-MesaTranslator.ps1`.

The donor's root license is GNU GPL version 2. Adapted files and this patch are
marked GPL-2.0-only, preserving that version rather than assuming permission
to relicense under later versions. Other shadPS4 files retain their notices.
See `LICENSES/GPL-2.0-only.txt`. This is an experimental adaptation, not an
endorsement or a claim that the KytyPS5 runtime/backend is fully integrated.

The new bridge intentionally does NOT copy Kyty's byte-pattern rewrite of
LLVM function attributes. These initial shaders contain no atomics; general
guest shaders/atomic semantics are not certified by this integration.
