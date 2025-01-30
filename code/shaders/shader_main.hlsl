#define MaxLightCount 8
#define LightType_Directional 0
#define LightType_Spot 1
#define LightType_Point 2
#define LightType_Count 3

struct Light
{
  float3    P;
  uint      type;
  // ------------- 16 -------------- //
  float4    intensity;
  // ------------- 16 -------------- //
  float3    dir;
  float     _pad_a;
  // ------------- 16 -------------- //
  float4x4  world_to_light;
  // ------------- 16 -------------- //
  float4x4  projection;
};

cbuffer Constant_Store0 : register(b0)
{
  float4x4 projection;
  float4x4 world_basis_to_camera_basis;
};

cbuffer Constant_Store1 : register(b1)
{
  float3     eye_p;
  uint       enable_texture;

  float      time_accum;
  uint       enable_reflections;
  uint       light_count;
  float      _pad_a[1];

  Light      lights[MaxLightCount];
};

struct Model_Instance
{
  float3x3 model_to_world_xform;
  float3x3 model_to_world_xform_inverse_transpose;
  float3 p;
};

struct VertexShader_Input
{
  float3 p         : IA_Position;
  float3 t         : IA_Tangent;
  float3 b         : IA_Bitangent;
  float3 n         : IA_Normal;
  float2 uv        : IA_TextureUV;
};

struct VertexShader_Output
{
  float4 p         : SV_Position;
  float4 colour    : ColourMod;
  float2 uv        : TextureUV;

  float3 world_p   : WorldP;
  float3 normal    : SurfaceNormal;
};

VertexShader_Output
vs_main(VertexShader_Input vs_inp, uint iid : SV_InstanceID)
{
  VertexShader_Output result = (VertexShader_Output)0;
  float3 world_p   = mul(instance.model_to_world_xform, vs_inp.p) + instance.p;
  float4 camera_p  = mul(world_to_camera, float4(world_p, 1.0f));

  result.p         = mul(proj, camera_p);
  result.colour    = float4(1.0f, 1.0f, 1.0f, 1.0f);
  result.uv        = vs_inp.uv;
  
  result.world_p   = world_p;
  result.normal    = vs_inp.n;
  return(result);
}

float4 ps_main(VertexShader_Output ps_inp) : SV_Target
{
  float4 sample_colour     = ps_inp.colour;
  return sample_colour;
}
