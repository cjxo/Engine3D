#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <timeapi.h>

#define COBJMACROS
#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <dxgidebug.h>
#include <d3d11sdklayers.h>
#include <d3dcompiler.h>

#include "base.h"
#include "my_math.h"
#include "os/os.h"

#include "my_math.c"
#include "os/os_win32.c"

static HWND   g_w32_window;
static s32    g_w32_window_width;
static s32    g_w32_window_height;

#if defined(ENGINE_DEBUG)
# define DX11_ShaderCompileFlags (D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR|D3DCOMPILE_SKIP_OPTIMIZATION|D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_WARNINGS_ARE_ERRORS)
#else
# define DX11_ShaderCompileFlags (D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR|D3DCOMPILE_OPTIMIZATION_LEVEL3)
#endif

static ID3D11Device                     *g_dx11_dev;
static ID3D11DeviceContext              *g_dx11_dev_cont;

static IDXGISwapChain1                  *g_dxgi_swap_chain;

// Render Target Stuff
static s32                               g_dx11_resolution_width  = 640;
static s32                               g_dx11_resolution_height = 360;
static ID3D11RenderTargetView           *g_dx11_back_buffer_rtv;

// Blend State
static ID3D11BlendState                 *g_dx11_blend_alpha;

// Rasterizer States
static ID3D11RasterizerState            *g_dx11_rasterizer_fill_cull_back_ccw;
static ID3D11RasterizerState            *g_dx11_rasterizer_fill_cull_front_ccw;
static ID3D11RasterizerState            *g_dx11_rasterizer_shadow_map_ccw;

// Samplers
static ID3D11SamplerState               *g_dx11_sampler_linear_all;
static ID3D11SamplerState               *g_dx11_sampler_point_all;

// Depth Stencil Views
static ID3D11DepthStencilView           *g_dx11_depth_stencil_dsv_main;

// Depth Stencil States
static ID3D11DepthStencilState          *g_dx11_depth_less_stencil_nope;

// Main renderer state
static ID3D11VertexShader               *g_dx11_vshader_main;
static ID3D11PixelShader                *g_dx11_pshader_main;
static ID3D11Buffer                     *g_dx11_cbuffer_main0;
static ID3D11Buffer                     *g_dx11_cbuffer_main1;
static ID3D11InputLayout                *g_dx11_input_layout;
static ID3D11Buffer                     *g_dx11_sbuffer_model_instances;
static ID3D11ShaderResourceView         *g_dx11_sbuffer_model_instances_srv;
static D3D11_VIEWPORT                    g_dx11_viewport_main;

// Static Textures
static ID3D11ShaderResourceView         *g_dx11_tex2d_brick_diffuse_srv;
static ID3D11ShaderResourceView         *g_dx11_tex2d_brick_normal_srv;
static ID3D11ShaderResourceView         *g_dx11_tex2d_brick_displace_srv;

static ID3D11ShaderResourceView         *g_dx11_tex2d_wood_diffuse_srv;
static ID3D11ShaderResourceView         *g_dx11_tex2d_wood_normal_srv;
static ID3D11ShaderResourceView         *g_dx11_tex2d_wood_displace_srv;

typedef struct
{
  v3f p;
  v3f tangent, bitangent, normal;
  v2f uv;
} Model_Vertex;

typedef struct
{
  m33 model_to_world_xform;
  m33 model_to_world_xform_it;
  v3f p;
} Model_Instance;

#define MaxLightCount 8
typedef u16 Light_Type;
enum
{
  LightType_Directional,
  LightType_Spot,
  LightType_Point,
  LightType_Count,
};

typedef struct
{
  v3f P;
  u32 type;
  // ------------- 16 -------------- //
  v4f intensity;
  // ------------- 16 -------------- //
  v3f dir;
  f32 _pad_a;
  // ------------- 16 -------------- //
  m44 world_to_light;
  // ------------- 16 -------------- //
  m44 projection;
} Light;

#define MaxModelInstances 512
typedef struct
{
  Model_Instance ins[MaxModelInstances];
  u64 count;
} Model_Instances;

__declspec(align(16)) typedef struct
{
  m44 projection;
  m44 world_basis_to_camera_basis;
} DX11_CBuffer_Main0;

__declspec(align(16)) typedef struct
{
  v3f eye_p;
  u32 enable_texture;
  // ------------- 16 -------------- //
  f32 time_accum;
  u32 enable_reflections;
  u32 light_count;
  f32 _pad_a[1];
  // ------------- 16 -------------- //
  
  Light lights[MaxLightCount];
  // ------------- sizeof(lights) % 16 == 0 -------------- //
} DX11_CBuffer_Main1;

typedef struct
{
  ID3D11Buffer *vbuffer, *ibuffer;
  u32 struct_size, index_count;
} DX11_Model;

static Light
create_directional_light(v3f P, v3f look_P, v4f intensity)
{
  Basis_R3 light_basis   = br3_from_center_to_target(P, look_P, (v3f){ 0.0f, 1.0f, 0.0f });
  
  Light result =
  {
    .P              = P,
    .type           = LightType_Directional,
    .intensity      = intensity,
    .dir            = v3f_sub(look_P, P),
    .world_to_light = {
                        light_basis.x.x, light_basis.y.x, light_basis.z.x, 0.0f,
                        light_basis.x.y, light_basis.y.y, light_basis.z.y, 0.0f,
                        light_basis.x.z, light_basis.y.z, light_basis.z.z, 0.0f,
                        -v3f_inner(light_basis.x, P), -v3f_inner(light_basis.y, P), -v3f_inner(light_basis.z, P), 1.0f
                      },
    .projection      = m44_make_orthographic_z01(-50, 50, -50, 50, 0.0f, 100.0f)
  };
  
  return(result);
}

static DX11_Model
dx11_create_model(f32 *vbuffer, u32 vbuffer_size_in_bytes, u32 struct_size,
                  u32 *ibuffer, u32 ibuffer_length)
{
  DX11_Model result;
  D3D11_BUFFER_DESC vbuffer_desc = 
  {
    .ByteWidth             = vbuffer_size_in_bytes,
    .Usage                 = D3D11_USAGE_IMMUTABLE,
    .BindFlags             = D3D11_BIND_VERTEX_BUFFER,
    .CPUAccessFlags        = 0,
    .MiscFlags             = 0,
    .StructureByteStride   = 0,
  };

  D3D11_SUBRESOURCE_DATA vbuffer_data =
  {
    .pSysMem           = vbuffer,
    .SysMemPitch       = struct_size,
    .SysMemSlicePitch  = 0,
  };

  AssertHR(ID3D11Device1_CreateBuffer(g_dx11_dev, &vbuffer_desc, &vbuffer_data, &result.vbuffer));
  
  D3D11_BUFFER_DESC ibuffer_desc =
  {
    .ByteWidth             = ibuffer_length * sizeof(u32),
    .Usage                 = D3D11_USAGE_IMMUTABLE,
    .BindFlags             = D3D11_BIND_INDEX_BUFFER,
    .CPUAccessFlags        = 0,
    .MiscFlags             = 0,
    .StructureByteStride   = 0,
  };

  D3D11_SUBRESOURCE_DATA ibuffer_data =
  {
    .pSysMem           = ibuffer,
    .SysMemPitch       = sizeof(u32),
    .SysMemSlicePitch  = 0,
  };

  AssertHR(ID3D11Device1_CreateBuffer(g_dx11_dev, &ibuffer_desc, &ibuffer_data, &result.ibuffer));

  result.struct_size = struct_size;
  result.index_count = ibuffer_length;
  return(result);
}

static ID3D11ShaderResourceView *
dx11_create_texture2d_mipmapped(char *filename)
{
  ID3D11Texture2D          *tex    = 0;
  ID3D11ShaderResourceView *result = 0;

  u32 texture_bpp               = 4;
  u32 texture_width             = 0;
  u32 texture_height            = 0;
  u8 *texture_data              = 0;

  D3D11_TEXTURE2D_DESC tex_desc;
  tex_desc.Width               = texture_width;
  tex_desc.Height              = texture_height;
  tex_desc.MipLevels           = 0;
  tex_desc.ArraySize           = 1;
  tex_desc.Format              = DXGI_FORMAT_R8G8B8A8_UNORM;
  tex_desc.SampleDesc.Count    = 1;
  tex_desc.SampleDesc.Quality  = 0;
  tex_desc.Usage               = D3D11_USAGE_DEFAULT;
  tex_desc.BindFlags           = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
  tex_desc.CPUAccessFlags      = 0;
  tex_desc.MiscFlags           = D3D11_RESOURCE_MISC_GENERATE_MIPS;


  AssertHR(ID3D11Device1_CreateTexture2D(g_dx11_dev, &tex_desc, 0, &tex));

  ID3D11DeviceContext_UpdateSubresource(g_dx11_dev_cont, (ID3D11Resource *)tex, 0, 0, texture_data, texture_width * texture_bpp, 0);

  D3D11_SHADER_RESOURCE_VIEW_DESC tex_srv_desc =
  {
    .Format             = tex_desc.Format,
    .ViewDimension      = D3D11_SRV_DIMENSION_TEXTURE2D,
    .Texture2D          = { .MostDetailedMip = 0, .MipLevels = (UINT)(-1) }
  };

  AssertHR(ID3D11Device1_CreateShaderResourceView(g_dx11_dev, (ID3D11Resource *)tex, &tex_srv_desc, &result));

  ID3D11DeviceContext_GenerateMips(g_dx11_dev_cont, result);
  return(result);
}

#define MapWidth 26
#define MapDepth 20
#define MapHeight 5
#define BrickDim 2.0f
#define PillarCount 5

static Model_Instance *
add_model_instance(Model_Instances *instances, v3f p, v3f scale, m33 rotate)
{
  Assert(instances->count < MaxModelInstances);
  Model_Instance *result = instances->ins + instances->count++;

  result->model_to_world_xform                  = m33_mul(m33_make_diag(scale), rotate);
  result->model_to_world_xform_it               = m33_mul(m33_make_diag((v3f) { 1.0f / scale.x, 1.0f / scale.y, 1.0f / scale.z }), rotate);
  result->p                                     = p;
  return(result);
}

static DX11_Model
create_plane_model(void)
{
  // p <-> tangent <-> bitangent <-> normal <-> uv
  f32 plane_vbuffer[] =
  {
    -0.5f, +0.5f, +0.0f,      +1.0f, +0.0f, +0.0f,    +0.0f, 1.0f, 0.0f,     +0.0f, +0.0f, -1.0f,         0.0f, 0.0f,
    -0.5f, -0.5f, +0.0f,      +1.0f, +0.0f, +0.0f,    +0.0f, 1.0f, 0.0f,     +0.0f, +0.0f, -1.0f,         0.0f, 1.0f,
    +0.5f, -0.5f, +0.0f,      +1.0f, +0.0f, +0.0f,    +0.0f, 1.0f, 0.0f,     +0.0f, +0.0f, -1.0f,         1.0f, 1.0f,
    +0.5f, +0.5f, +0.0f,      +1.0f, +0.0f, +0.0f,    +0.0f, 1.0f, 0.0f,     +0.0f, +0.0f, -1.0f,         1.0f, 0.0f,
  };

  u32 plane_ibuffer[] = { 0, 1, 2, 2, 3, 0 };
  
  return dx11_create_model(plane_vbuffer, sizeof(plane_vbuffer), sizeof(Model_Vertex), 
                           plane_ibuffer, ArrayCount(plane_ibuffer));
}

static DX11_Model
create_sphere_model(f32 radius, u32 quality)
{
  HANDLE heap = GetProcessHeap();
  DX11_Model result;

  u32 horizontal = quality * 2;
  u32 vertical   = quality * 2;
  f32 theta_step = (2.0f * PIF32) / (f32)horizontal;

  u32             vertex_count        = (vertical + 1) * (horizontal + 1);
  u32             vertex_index        = 0;
  Model_Vertex   *vertices            = HeapAlloc(heap, HEAP_GENERATE_EXCEPTIONS | HEAP_NO_SERIALIZE, vertex_count * sizeof(Model_Vertex));

  u32 index_count   = vertical * horizontal * 6;
  u32 index_index   = 0;
  u32 *indices      = HeapAlloc(heap, HEAP_GENERATE_EXCEPTIONS | HEAP_NO_SERIALIZE, index_count * sizeof(u32));

  for (u32 vert = 0; vert < (vertical + 1); ++vert)
  {
    for (u32 hori = 0; hori < (horizontal + 1); ++hori)
    {
      f32 c = cosf(hori * theta_step);
      f32 s = sinf(vert * theta_step);

      f32 cc = cosf(vert * theta_step);
      f32 ss = sinf(hori * theta_step);

      f32 x = c * s;
      f32 y = cc;
      f32 z = ss * s;
      v3f tangent     = { -(f32)hori * ss * s + (f32)vert * c * cc, -(f32)vert * s, (f32)hori * c * s + (f32)vert * ss * cc };
      v3f normal      = { x, y, z };
      vertices[vertex_index++] = (Model_Vertex)
      {
        .p           = { radius * x, radius * y, radius * z },
        .uv          = { (f32)hori / (f32)horizontal, (f32)vert / (f32)vertical },
        .normal      = normal,
        .tangent     = tangent,
        .bitangent   = v3f_cross(tangent, normal)
      };
    }
  }
  
  Assert(vertex_index == vertex_count);

  for (u32 vert = 0; vert < vertical; ++vert)
  {
    for (u32 hori = 0; hori < horizontal; ++hori)
    {
      indices[index_index++] = (vert + 1) * (horizontal + 1) + (hori + 0);
      indices[index_index++] = (vert + 0) * (horizontal + 1) + (hori + 0);
      indices[index_index++] = (vert + 0) * (horizontal + 1) + (hori + 1);
      
      indices[index_index++] = (vert + 0) * (horizontal + 1) + (hori + 1);
      indices[index_index++] = (vert + 1) * (horizontal + 1) + (hori + 1);
      indices[index_index++] = (vert + 1) * (horizontal + 1) + (hori + 0);
    }
  }

  Assert(index_index == index_count);
  result = dx11_create_model((f32 *)vertices, vertex_count * sizeof(Model_Vertex),
                             sizeof(Model_Vertex),
                             indices, index_count);

  HeapFree(heap, HEAP_GENERATE_EXCEPTIONS | HEAP_NO_SERIALIZE, vertices);
  HeapFree(heap, HEAP_GENERATE_EXCEPTIONS | HEAP_NO_SERIALIZE, indices);
  return(result);
}


static DX11_Model
create_cylinder_model(f32 bottom_radius, f32 top_radius,
                      f32 height, u32 slice_count, u32 stack_count)
{
  HANDLE heap = GetProcessHeap();
  DX11_Model result;

  f32 stack_height   = height / (f32)stack_count;
  f32 delta_radius   = (top_radius - bottom_radius) / (f32)stack_count;
  f32 delta_theta    = (2.0f * PIF32) / (f32)slice_count;
  u32 ring_count     = stack_count + 1;

  u32             vertex_count    = ring_count * (slice_count + 1) + (slice_count + 1) * 2 + 2;
  u32             vertex_index    = 0;
  Model_Vertex   *vertices        = HeapAlloc(heap, HEAP_GENERATE_EXCEPTIONS | HEAP_NO_SERIALIZE, vertex_count * sizeof(Model_Vertex));

  u32 index_count   = stack_count * slice_count * 6 + slice_count * 6;
  u32 index_index   = 0;
  u32 *indices      = HeapAlloc(heap, HEAP_GENERATE_EXCEPTIONS | HEAP_NO_SERIALIZE, index_count * sizeof(u32));
  for (u32 ring_idx = 0; ring_idx < ring_count; ++ring_idx)
  {
    f32 y = -0.5f * height + (f32)ring_idx * stack_height;
    f32 r = bottom_radius + (f32)ring_idx * delta_radius;

    for (u32 slice_idx = 0; slice_idx <= slice_count; ++slice_idx)
    {
      Model_Vertex vertex;

      f32 c = cosf((f32)slice_idx * delta_theta);
      f32 s = sinf((f32)slice_idx * delta_theta);

      f32 dr           = bottom_radius - top_radius;
      vertex.p         = (v3f) { r * c, y, r * s };
      vertex.uv        = (v2f) { (f32)slice_idx / (f32)slice_count, 1.0f - (f32)ring_idx / (f32)stack_count };
      vertex.tangent   = (v3f) {  -s, 0, c };
      vertex.bitangent = (v3f) { dr * c, -height, dr * s };
      vertex.normal    = v3f_cross(vertex.tangent, vertex.bitangent);

      vertices[vertex_index++] = vertex;
    }
  }

  u32 start_top_idx = vertex_index;
  f32 top_y = height * 0.5f;
  for (u32 slice_idx = 0; slice_idx <= slice_count; ++slice_idx)
  {
    f32 x = top_radius * cosf(slice_idx * delta_theta);
    f32 z = top_radius * sinf(slice_idx * delta_theta);
    
    Model_Vertex vertex =
    {
      .p          = { x, top_y, z },
      .uv         = { x / height + 0.5f, z / height + 0.5f },
      .tangent    = { 1.0f, 0.0f, 0.0f },
      .normal     = { 0.0f, 1.0f, 0.0f },
      .bitangent  = { 0.0f, 0.0f, 1.0f },
    };

    vertices[vertex_index++] = vertex;
  }

  u32 top_center_idx = vertex_index++;
  vertices[top_center_idx] = (Model_Vertex)
  {
    .p           = { 0.0f, top_y, 0.0f },
    .uv          = { 0.5f, 0.5f },
    .tangent     = { 1.0f, 0.0f, 0.0f },
    .normal      = { 0.0f, 1.0f, 0.0f },
    .bitangent   = { 0.0f, 0.0f, 1.0f },
  };
  
  u32 start_bottom_idx = vertex_index;
  f32 bottom_y = -height * 0.5f;
  for (u32 slice_idx = 0; slice_idx <= slice_count; ++slice_idx)
  {
    f32 x = bottom_radius * cosf(slice_idx * delta_theta);
    f32 z = bottom_radius * sinf(slice_idx * delta_theta);
    
    Model_Vertex vertex =
    {
      .p          = { x, bottom_y, z },
      .uv         = { x / height + 0.5f, z / height + 0.5f },
      .tangent    = { 1.0f, 0.0f, 0.0f },
      .normal     = { 0.0f, -1.0f, 0.0f },
      .bitangent  = { 0.0f, 0.0f, -1.0f },
    };

    vertices[vertex_index++] = vertex;
  }

  u32 bottom_center_idx = vertex_index++;
  vertices[bottom_center_idx] = (Model_Vertex)
  {
    .p           = { 0.0f, bottom_y, 0.0f },
    .uv          = { 0.5f, 0.5f },
    .tangent     = { 1.0f, 0.0f, 0.0f },
    .normal      = { 0.0f, -1.0f, 0.0f },
    .bitangent   = { 0.0f, 0.0f, -1.0f },
  };

  Assert(vertex_index == vertex_count);

  for (u32 stack_idx = 0; stack_idx < stack_count; ++stack_idx)
  {
    for (u32 slice_idx = 0; slice_idx < slice_count; ++slice_idx)
    {
      indices[index_index++] = (stack_idx + 1) * (slice_count + 1) + (slice_idx + 0);
      indices[index_index++] = (stack_idx + 0) * (slice_count + 1) + (slice_idx + 0);
      indices[index_index++] = (stack_idx + 0) * (slice_count + 1) + (slice_idx + 1);
      
      indices[index_index++] = (stack_idx + 0) * (slice_count + 1) + (slice_idx + 1);
      indices[index_index++] = (stack_idx + 1) * (slice_count + 1) + (slice_idx + 1);
      indices[index_index++] = (stack_idx + 1) * (slice_count + 1) + (slice_idx + 0);
    }
  }

  for (u32 slice_idx = 0; slice_idx < slice_count; ++slice_idx)
  {
    indices[index_index++] = top_center_idx;
    indices[index_index++] = start_top_idx + slice_idx;
    indices[index_index++] = start_top_idx + slice_idx + 1;
  }

  for (u32 slice_idx = 0; slice_idx < slice_count; ++slice_idx)
  {
    indices[index_index++] = bottom_center_idx;
    indices[index_index++] = start_bottom_idx + slice_idx + 1;
    indices[index_index++] = start_bottom_idx + slice_idx;
  }
  Assert(index_index == index_count);

  result = dx11_create_model((f32 *)vertices, vertex_count * sizeof(Model_Vertex), sizeof(Model_Vertex),
                             indices, index_count);

  HeapFree(heap, HEAP_GENERATE_EXCEPTIONS | HEAP_NO_SERIALIZE, vertices);
  HeapFree(heap, HEAP_GENERATE_EXCEPTIONS | HEAP_NO_SERIALIZE, indices);
  return(result);
}


static DX11_Model
create_cube_model(void)
{
  // p <-> tangent <-> bitangent <-> normal <-> uv
  f32 vbuffer[] =
  {
    // front face
    -0.5f, +0.5f, -0.5f,      +1.0f, +0.0f, +0.0f,    +0.0f, 1.0f, 0.0f,     +0.0f, +0.0f, -1.0f,         0.0f, 0.0f,
    -0.5f, -0.5f, -0.5f,      +1.0f, +0.0f, +0.0f,    +0.0f, 1.0f, 0.0f,     +0.0f, +0.0f, -1.0f,         0.0f, 1.0f,
    +0.5f, -0.5f, -0.5f,      +1.0f, +0.0f, +0.0f,    +0.0f, 1.0f, 0.0f,     +0.0f, +0.0f, -1.0f,         1.0f, 1.0f,
    +0.5f, +0.5f, -0.5f,      +1.0f, +0.0f, +0.0f,    +0.0f, 1.0f, 0.0f,     +0.0f, +0.0f, -1.0f,         1.0f, 0.0f,

    // right face
    +0.5f, +0.5f, -0.5f,     +0.0f, +0.0f, +1.0f,     +0.0f, +1.0f, 0.0f,    +1.0f, +0.0f, +0.0f,     0.0f, 0.0f,
    +0.5f, -0.5f, -0.5f,     +0.0f, +0.0f, +1.0f,     +0.0f, +1.0f, 0.0f,    +1.0f, +0.0f, +0.0f,     0.0f, 1.0f,
    +0.5f, -0.5f, +0.5f,     +0.0f, +0.0f, +1.0f,     +0.0f, +1.0f, 0.0f,    +1.0f, +0.0f, +0.0f,     1.0f, 1.0f,
    +0.5f, +0.5f, +0.5f,     +0.0f, +0.0f, +1.0f,     +0.0f, +1.0f, 0.0f,    +1.0f, +0.0f, +0.0f,     1.0f, 0.0f,

    // back face
    +0.5f, +0.5f, +0.5f,     -1.0f, +0.0f, +0.0f,     +0.0f, +1.0f, +0.0f,   +0.0f, +0.0f, +1.0f,     0.0f, 0.0f,
    +0.5f, -0.5f, +0.5f,     -1.0f, +0.0f, +0.0f,     +0.0f, +1.0f, +0.0f,   +0.0f, +0.0f, +1.0f,     0.0f, 1.0f,
    -0.5f, -0.5f, +0.5f,     -1.0f, +0.0f, +0.0f,     +0.0f, +1.0f, +0.0f,   +0.0f, +0.0f, +1.0f,     1.0f, 1.0f,
    -0.5f, +0.5f, +0.5f,     -1.0f, +0.0f, +0.0f,     +0.0f, +1.0f, +0.0f,   +0.0f, +0.0f, +1.0f,     1.0f, 0.0f,

    // left face
    -0.5f, +0.5f, +0.5f,     +0.0f, +0.0f, -1.0f,     +0.0f, +1.0f, +0.0f,   -1.0f, +0.0f, +0.0f,     0.0f, 0.0f,
    -0.5f, -0.5f, +0.5f,     +0.0f, +0.0f, -1.0f,     +0.0f, +1.0f, +0.0f,   -1.0f, +0.0f, +0.0f,     0.0f, 1.0f,
    -0.5f, -0.5f, -0.5f,     +0.0f, +0.0f, -1.0f,     +0.0f, +1.0f, +0.0f,   -1.0f, +0.0f, +0.0f,     1.0f, 1.0f,
    -0.5f, +0.5f, -0.5f,     +0.0f, +0.0f, -1.0f,     +0.0f, +1.0f, +0.0f,   -1.0f, +0.0f, +0.0f,     1.0f, 0.0f,

    // top face
    -0.5f, +0.5f, +0.5f,     +1.0f, +0.0f, +0.0f,     +0.0f, +0.0f, +1.0f,   +0.0f, +1.0f, +0.0f,     0.0f, 0.0f,
    -0.5f, +0.5f, -0.5f,     +1.0f, +0.0f, +0.0f,     +0.0f, +0.0f, +1.0f,   +0.0f, +1.0f, +0.0f,     0.0f, 1.0f,
    +0.5f, +0.5f, -0.5f,     +1.0f, +0.0f, +0.0f,     +0.0f, +0.0f, +1.0f,   +0.0f, +1.0f, +0.0f,     1.0f, 1.0f,
    +0.5f, +0.5f, +0.5f,     +1.0f, +0.0f, +0.0f,     +0.0f, +0.0f, +1.0f,   +0.0f, +1.0f, +0.0f,     1.0f, 0.0f,

    // bottom face
    -0.5f, -0.5f, -0.5f,     +1.0f, +0.0f, +0.0f,     +0.0f, +0.0f, -1.0f,   +0.0f, -1.0f, +0.0f,     0.0f, 0.0f,
    -0.5f, -0.5f, +0.5f,     +1.0f, +0.0f, +0.0f,     +0.0f, +0.0f, -1.0f,   +0.0f, -1.0f, +0.0f,     0.0f, 1.0f,
    +0.5f, -0.5f, +0.5f,     +1.0f, +0.0f, +0.0f,     +0.0f, +0.0f, -1.0f,   +0.0f, -1.0f, +0.0f,     1.0f, 1.0f,
    +0.5f, -0.5f, -0.5f,     +1.0f, +0.0f, +0.0f,     +0.0f, +0.0f, -1.0f,   +0.0f, -1.0f, +0.0f,     1.0f, 0.0f,
  };

  u32 ibuffer[] = {
    0, 1, 2,
    2, 3, 0,

    4, 5, 6,
    6, 7, 4,

    8, 9, 10,
    10, 11, 8,

    12, 13, 14,
    14, 15, 12,

    16, 17, 18,
    18, 19, 16,

    20, 21, 22,
    22, 23, 20,
  };
  
  return dx11_create_model(vbuffer, sizeof(vbuffer), sizeof(Model_Vertex), 
                           ibuffer, ArrayCount(ibuffer));
}

static void
dx11_create_devices(void)
{
  b32 success = false;
  UINT flags = (D3D11_CREATE_DEVICE_SINGLETHREADED);
#if defined(ENGINE_DEBUG)
  flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif        
  D3D_FEATURE_LEVEL feature_levels = D3D_FEATURE_LEVEL_11_0;
  if(SUCCEEDED(D3D11CreateDevice(0, D3D_DRIVER_TYPE_HARDWARE, 0, flags, &feature_levels, 1,
                                 D3D11_SDK_VERSION, &g_dx11_dev, 0, &g_dx11_dev_cont)))
  {
#if defined(ENGINE_DEBUG)
    ID3D11InfoQueue *dx11_infoq;
    IDXGIInfoQueue *dxgi_infoq;
    
    if (SUCCEEDED(ID3D11Device_QueryInterface(g_dx11_dev, &IID_ID3D11InfoQueue, &dx11_infoq)))
    {
      ID3D11InfoQueue_SetBreakOnSeverity(dx11_infoq, D3D11_MESSAGE_SEVERITY_CORRUPTION, TRUE);
      ID3D11InfoQueue_SetBreakOnSeverity(dx11_infoq, D3D11_MESSAGE_SEVERITY_ERROR, TRUE);
      ID3D11InfoQueue_Release(dx11_infoq);
      
      typedef HRESULT (* DXGIGetDebugInterfaceFN)(REFIID, void **);

      HMODULE lib = LoadLibraryA("Dxgidebug.dll");
      if (lib)
      {
        DXGIGetDebugInterfaceFN fn = (DXGIGetDebugInterfaceFN)GetProcAddress(lib, "DXGIGetDebugInterface");
        if (fn)
        {
          if (SUCCEEDED(fn(&IID_IDXGIInfoQueue, &dxgi_infoq)))
          {
            IDXGIInfoQueue_SetBreakOnSeverity(dxgi_infoq, DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, TRUE);
            IDXGIInfoQueue_SetBreakOnSeverity(dxgi_infoq, DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, TRUE);
            IDXGIInfoQueue_Release(dxgi_infoq);
            success = true;
          }
        }
        
        FreeLibrary(lib);
      }
    }
#else
    success = true;
#endif
  }
  
  if (!success)
  {
    // TODO: Error 
    ExitProcess(1);
  }
}


static void
dx11_create_swap_chain(void)
{
  b32 success = false;

  IDXGIDevice2      *dxgi_device;
  IDXGIAdapter      *dxgi_adapter;
  IDXGIFactory2     *dxgi_factory;
  ID3D11Texture2D   *back_buffer_tex;
  if (SUCCEEDED(ID3D11Device_QueryInterface(g_dx11_dev, &IID_IDXGIDevice2, &dxgi_device)))
  {
    if (SUCCEEDED(IDXGIDevice2_GetAdapter(dxgi_device, &dxgi_adapter)))
    {
      if (SUCCEEDED(IDXGIAdapter_GetParent(dxgi_adapter, &IID_IDXGIFactory2, &dxgi_factory)))
      {
      // https://learn.microsoft.com/en-us/windows/win32/api/dxgi/ne-dxgi-dxgi_swap_effect
        DXGI_SWAP_CHAIN_DESC1 swap_chain_desc = {0};
        swap_chain_desc.Width              = g_dx11_resolution_width;
        swap_chain_desc.Height             = g_dx11_resolution_height;
        swap_chain_desc.Format             = DXGI_FORMAT_B8G8R8A8_UNORM;
        swap_chain_desc.Stereo             = FALSE;
        swap_chain_desc.SampleDesc.Count   = 1;
        swap_chain_desc.SampleDesc.Quality = 0;
        swap_chain_desc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swap_chain_desc.BufferCount        = 2;
        swap_chain_desc.Scaling            = DXGI_SCALING_STRETCH;
        swap_chain_desc.SwapEffect         = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swap_chain_desc.AlphaMode          = DXGI_ALPHA_MODE_UNSPECIFIED;
        swap_chain_desc.Flags              = 0;
        
        if (SUCCEEDED(IDXGIFactory2_CreateSwapChainForHwnd(dxgi_factory, (IUnknown *)g_dx11_dev, g_w32_window,
                                                           &swap_chain_desc, 0, 0,
                                                           &g_dxgi_swap_chain)))
        {
          IDXGIFactory2_MakeWindowAssociation(dxgi_factory, g_w32_window, DXGI_MWA_NO_ALT_ENTER);
          
          if (SUCCEEDED(IDXGISwapChain1_GetBuffer(g_dxgi_swap_chain, 0, &IID_ID3D11Texture2D, &back_buffer_tex)))
          {
            D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = { 0 };
            rtv_desc.Format              = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
            rtv_desc.ViewDimension       = D3D11_RTV_DIMENSION_TEXTURE2D;
            rtv_desc.Texture2D.MipSlice  = 0;
            if (SUCCEEDED(ID3D11Device_CreateRenderTargetView(g_dx11_dev, (ID3D11Resource *)back_buffer_tex,
                                                              &rtv_desc, &g_dx11_back_buffer_rtv)))
            {
              success = true;
            }

            ID3D11Texture2D_Release(back_buffer_tex);
          }
        }
        
        IDXGIFactory2_Release(dxgi_factory);
      }
      
      IDXGIAdapter_Release(dxgi_adapter);
    }
    
    IDXGIDevice2_Release(dxgi_device);
  }
  
  if (!success)
  {
    // TODO: Error 
    ExitProcess(1);
  }
}

static void
dx11_create_blend_states(void)
{
  D3D11_BLEND_DESC blend_desc;
  blend_desc.AlphaToCoverageEnable                 = FALSE;
  blend_desc.IndependentBlendEnable                = FALSE;
  blend_desc.RenderTarget[0].BlendEnable           = TRUE;
  blend_desc.RenderTarget[0].SrcBlend              = D3D11_BLEND_SRC_ALPHA;
  blend_desc.RenderTarget[0].DestBlend             = D3D11_BLEND_INV_SRC_ALPHA;
  blend_desc.RenderTarget[0].BlendOp               = D3D11_BLEND_OP_ADD;
  blend_desc.RenderTarget[0].SrcBlendAlpha         = D3D11_BLEND_SRC_ALPHA;
  blend_desc.RenderTarget[0].DestBlendAlpha        = D3D11_BLEND_INV_SRC_ALPHA;
  blend_desc.RenderTarget[0].BlendOpAlpha          = D3D11_BLEND_OP_ADD;
  blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  AssertHR(ID3D11Device_CreateBlendState(g_dx11_dev, &blend_desc, &g_dx11_blend_alpha));
}

static void
dx11_create_rasterizer_states(void)
{
  D3D11_RASTERIZER_DESC raster_desc;
  raster_desc.FillMode                     = D3D11_FILL_SOLID;
  raster_desc.CullMode                     = D3D11_CULL_BACK;
  raster_desc.FrontCounterClockwise        = TRUE;
  raster_desc.DepthBias                    = 0;
  raster_desc.DepthBiasClamp               = 0.0f;
  raster_desc.SlopeScaledDepthBias         = 0.0f;
  raster_desc.DepthClipEnable              = TRUE;
  raster_desc.ScissorEnable                = FALSE;
  raster_desc.MultisampleEnable            = FALSE;
  raster_desc.AntialiasedLineEnable        = FALSE;
  AssertHR(ID3D11Device1_CreateRasterizerState(g_dx11_dev, &raster_desc, &g_dx11_rasterizer_fill_cull_back_ccw));

  raster_desc.FillMode                     = D3D11_FILL_SOLID;
  raster_desc.CullMode                     = D3D11_CULL_FRONT;
  raster_desc.FrontCounterClockwise        = TRUE;
  raster_desc.DepthBias                    = 0;
  raster_desc.DepthBiasClamp               = 0.0f;
  raster_desc.SlopeScaledDepthBias         = 0.0f;
  raster_desc.DepthClipEnable              = TRUE;
  raster_desc.ScissorEnable                = FALSE;
  raster_desc.MultisampleEnable            = FALSE;
  raster_desc.AntialiasedLineEnable        = FALSE;
  AssertHR(ID3D11Device1_CreateRasterizerState(g_dx11_dev, &raster_desc, &g_dx11_rasterizer_fill_cull_front_ccw));
  
  raster_desc.FillMode                     = D3D11_FILL_SOLID;
  raster_desc.CullMode                     = D3D11_CULL_BACK;
  raster_desc.FrontCounterClockwise        = TRUE;
  raster_desc.DepthBias                    = 10000;
  raster_desc.DepthBiasClamp               = 0.0f;
  raster_desc.SlopeScaledDepthBias         = 1.0f;
  raster_desc.DepthClipEnable              = TRUE;
  raster_desc.ScissorEnable                = FALSE;
  raster_desc.MultisampleEnable            = FALSE;
  raster_desc.AntialiasedLineEnable        = FALSE;
  AssertHR(ID3D11Device1_CreateRasterizerState(g_dx11_dev, &raster_desc, &g_dx11_rasterizer_shadow_map_ccw));
}

static void
dx11_create_sampler_states(void)
{
  D3D11_SAMPLER_DESC sam_desc;
  //sam_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
  //sam_desc.Filter              = D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR;
  sam_desc.Filter              = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  //sam_desc.Filter = D3D11_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR;
  //sam_desc.Filter = D3D11_FILTER_ANISOTROPIC;
  sam_desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
  sam_desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
  sam_desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
  sam_desc.MipLODBias = 0;
  //sam_desc.MaxAnisotropy = D3D11_REQ_MAXANISOTROPY;
  sam_desc.MaxAnisotropy = 1;
  sam_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
  sam_desc.BorderColor[0] = 1.0f;
  sam_desc.BorderColor[1] = 1.0f;
  sam_desc.BorderColor[2] = 1.0f;
  sam_desc.BorderColor[3] = 1.0f;
  sam_desc.MinLOD = 0;
  sam_desc.MaxLOD = D3D11_FLOAT32_MAX;

  AssertHR(ID3D11Device1_CreateSamplerState(g_dx11_dev, &sam_desc, &g_dx11_sampler_linear_all));

  sam_desc.Filter   = D3D11_FILTER_MIN_MAG_MIP_POINT;
  sam_desc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
  sam_desc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
  sam_desc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
  AssertHR(ID3D11Device1_CreateSamplerState(g_dx11_dev, &sam_desc, &g_dx11_sampler_point_all));
}

static void
dx11_create_depth_stencil_states(void)
{
  D3D11_DEPTH_STENCIL_DESC depth_stencil_desc =
  {
    .DepthEnable           = TRUE,
    .DepthWriteMask        = D3D11_DEPTH_WRITE_MASK_ALL,
    .DepthFunc             = D3D11_COMPARISON_LESS,
    .StencilEnable         = FALSE,
    .StencilReadMask       = D3D11_DEFAULT_STENCIL_READ_MASK,
    .StencilWriteMask      = D3D11_DEFAULT_STENCIL_WRITE_MASK,
  };

  AssertHR(ID3D11Device1_CreateDepthStencilState(g_dx11_dev, &depth_stencil_desc, &g_dx11_depth_less_stencil_nope));
}

int __stdcall
WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, int nCmdShow)
{
  (void)hInstance;
  (void)hPrevInstance;
  (void)lpCmdLine;
  (void)nCmdShow;

  WNDCLASS wnd_class =
  {
    .style           = 0,
    .lpfnWndProc     = &w32_window_proc,
    .cbClsExtra      = 0,
    .cbWndExtra      = 0,
    .hInstance       = GetModuleHandleA(0),
    .hIcon           = LoadIconA(0, IDI_APPLICATION),
    .hCursor         = LoadCursorA(0, IDC_ARROW),
    .hbrBackground   = GetStockObject(BLACK_BRUSH),
    .lpszMenuName    = 0,
    .lpszClassName   = "Game Project",
  };

  g_w32_window_width   = 1280;
  g_w32_window_height  = 720;
  RegisterClassA(&wnd_class);
  RECT w32_client_rect =
  {
    .left       = 0,
    .top        = 0,
    .right      = g_w32_window_width,
    .bottom     = g_w32_window_height,
  };
  AdjustWindowRect(&w32_client_rect, WS_OVERLAPPEDWINDOW, FALSE);
  
  g_w32_window  = CreateWindowA(wnd_class.lpszClassName, "D3D11 - HLSL", WS_OVERLAPPEDWINDOW,
                                0, 0, w32_client_rect.right - w32_client_rect.left, w32_client_rect.bottom - w32_client_rect.top,
                                0, 0, wnd_class.hInstance, 0);
  AssertTrue(IsWindow(g_w32_window));

  ShowWindow(g_w32_window, SW_SHOW);

  dx11_create_devices();
  dx11_create_swap_chain();
  dx11_create_blend_states();
  dx11_create_rasterizer_states();
  dx11_create_sampler_states();
  dx11_create_depth_stencil_states();

  b32 is_running = true;
  TIMECAPS tc;
  timeGetDevCaps(&tc, sizeof(tc));
  timeBeginPeriod(tc.wPeriodMin);
  
  DEVMODE devmode;
  EnumDisplaySettingsA(0, ENUM_CURRENT_SETTINGS, &devmode);
  f32 seconds_per_frame   = 1.0f / (f32)devmode.dmDisplayFrequency;
  f32 game_update_secs    = 1.0f / (f32)devmode.dmDisplayFrequency;

  LARGE_INTEGER perf_freq;
  QueryPerformanceFrequency(&perf_freq);
  
  LARGE_INTEGER perf_count_begin;
  QueryPerformanceCounter(&perf_count_begin);

  OS_InputFlag input_key[OS_KeyType_Count] = { 0 };

  g_dx11_viewport_main = (D3D11_VIEWPORT)
  {
    .Width     = (f32)g_dx11_resolution_width,
    .Height    = (f32)g_dx11_resolution_height,
    .TopLeftX  = 0,
    .TopLeftY  = 0,
    .MinDepth  = 0,
    .MaxDepth  = 1,
  };
  
  // renderer state stuff
  b32 camera_roam        = false;
  v3f camera_p           = {0};
  f32 camera_rotate_yz   = -90;
  f32 camera_rotate_xz   = 90;
  f32 camera_sens        = 0.1f;
  f32 camera_move_comp   = 8.0f;

  while (is_running)
  {
    for (u32 key = 0; key < OS_KeyType_Count; ++key)
    {
      input_key[key] &= ~(OS_InputFlag_Pressed | OS_InputFlag_Released);
    }

    MSG msg;
    while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE) != 0)
    {
      switch (msg.message)
      {
        case WM_QUIT:
        {
          is_running = 0;
        } break;

        case WM_KEYDOWN:
        {
          OS_KeyType key = w32_map_wparam_to_keytype(msg.wParam);
          if (key != OS_KeyType_Count)
          {
            input_key[key] |= OS_InputFlag_Pressed | OS_InputFlag_Held;
          }
        } break;

        case WM_KEYUP:
        {
          OS_KeyType key = w32_map_wparam_to_keytype(msg.wParam);
          if (key != OS_KeyType_Count)
          {
            input_key[key] |= OS_InputFlag_Released;
            input_key[key] &= ~OS_InputFlag_Held;
          }
        } break;

        default:
        {
          TranslateMessage(&msg); 
          DispatchMessage(&msg);
        } break;
      }
    }

    if (input_key[OS_KeyType_Esc] & OS_InputFlag_Released)
    {
      camera_roam = !camera_roam;
    }

    if (camera_roam)
    {
      f32 mouse_delta_x, mouse_delta_y;
      
      POINT cursor_p;
      GetCursorPos(&cursor_p);
      
      ScreenToClient(g_w32_window, &cursor_p);
      
      POINT middle;
      middle.x = g_w32_window_width / 2;
      middle.y = g_w32_window_height / 2;
      ClientToScreen(g_w32_window, &middle);
      SetCursorPos(middle.x, middle.y);
      
      mouse_delta_x = (f32)((g_w32_window_width * 0.5f) - cursor_p.x) * camera_sens * game_update_secs;
      mouse_delta_y = (f32)(cursor_p.y - (g_w32_window_height * 0.5f)) * camera_sens * game_update_secs;
      
      camera_rotate_yz += mouse_delta_y;
      camera_rotate_xz += mouse_delta_x;
      if (camera_rotate_yz < -179.0f)
      {
        camera_rotate_yz = -179.0f;
      }
      
      if (camera_rotate_yz > 179.0f)
      {
        camera_rotate_yz = 179.0f;
      }
      
      if (camera_rotate_yz >= 360.0f)
      {
        camera_rotate_xz = 0.0f;
      }
      
      if (camera_rotate_xz <= 0.0f)
      {
        camera_rotate_xz = 360.0f;
      } 
    }

    f32 move_comp    = camera_move_comp * game_update_secs;
    f32 xz           = Radians(camera_rotate_xz);
    f32 yz           = Radians(camera_rotate_yz);
    v3f temp_up      = (v3f){ 0.0f, 1.0f, 0.0f };
    v3f camera_front = v3f_normalized((v3f){ cosf(xz) * sinf(yz), cosf(yz), sinf(xz) * sinf(yz) });

    f32 scaling          = v3f_inner(temp_up, camera_front);
    v3f camera_up        = v3f_normalized(v3f_sub(temp_up, v3f_scale(scaling, camera_front)));
    v3f camera_right     = v3f_normalized(v3f_cross(camera_up, camera_front));

    if (input_key[OS_KeyType_W] & OS_InputFlag_Held)
    {
      v3f_add_eq(&camera_p, v3f_scale(move_comp, camera_front));
    }

    if (input_key[OS_KeyType_A] & OS_InputFlag_Held)
    {
      v3f_sub_eq(&camera_p, v3f_scale(move_comp, camera_right));
    }

    if (input_key[OS_KeyType_S] & OS_InputFlag_Held)
    {
      v3f_sub_eq(&camera_p, v3f_scale(move_comp, camera_front));
    }

    if (input_key[OS_KeyType_D] & OS_InputFlag_Held)
    {
      v3f_add_eq(&camera_p, v3f_scale(move_comp, camera_right));
    }

    if (input_key[OS_KeyType_Space] & OS_InputFlag_Held)
    {
      v3f_add_eq(&camera_p, v3f_scale(move_comp, camera_up));
    }

    if (input_key[OS_KeyType_X] & OS_InputFlag_Held)
    {
      v3f_sub_eq(&camera_p, v3f_scale(move_comp, camera_up));
    }

    m44 world_to_camera = (m44)
    {
      camera_right.x, camera_up.x, camera_front.x, 0.0f,
      camera_right.y, camera_up.y, camera_front.y, 0.0f,
      camera_right.z, camera_up.z, camera_front.z, 0.0f,
      -v3f_inner(camera_right, camera_p), -v3f_inner(camera_up, camera_p), -v3f_inner(camera_front, camera_p), 1.0f
    };

    m44 projection = m44_make_perspective_z01(g_dx11_viewport_main.Height / g_dx11_viewport_main.Width, Radians(66.2f), 0.1f, 100.0f);

    float clear_colour[4] = {0};
    ID3D11DeviceContext_ClearRenderTargetView(g_dx11_dev_cont, g_dx11_back_buffer_rtv, clear_colour);

    ID3D11DeviceContext_ClearState(g_dx11_dev_cont);
    IDXGISwapChain1_Present(g_dxgi_swap_chain, 1, 0);

    LARGE_INTEGER perf_count_end;
    QueryPerformanceCounter(&perf_count_end);
    
    f32 seconds_of_work = (f32)(perf_count_end.QuadPart - perf_count_begin.QuadPart) / (f32)perf_freq.QuadPart;
    if (seconds_of_work < seconds_per_frame)
    {
      Sleep((u32)((seconds_per_frame - seconds_of_work) * 1000.0f));
    }
    else
    {
      //missed frame
    }
    
    QueryPerformanceCounter(&perf_count_begin);
  }
  ExitProcess(0);
}
