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
  float4 colour;
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

StructuredBuffer<Model_Instance> g_model_instances : register(t0);

VertexShader_Output
vs_main(VertexShader_Input vs_inp, uint iid : SV_InstanceID)
{
  VertexShader_Output result = (VertexShader_Output)0;

  Model_Instance instance = g_model_instances[iid];
  float3 world_p          = mul(instance.model_to_world_xform, vs_inp.p) + instance.p;
  float4 camera_p         = mul(world_basis_to_camera_basis, float4(world_p, 1.0f));

  result.p         = mul(projection, camera_p);
  result.colour    = instance.colour;
  result.uv        = vs_inp.uv;
  
  result.world_p   = world_p;
  result.normal    = vs_inp.n;
  return(result);
}

// Given an energy x to a pixel, to the monitor, it would appear x^(2).
// The display transfer function (DTF) describes the relationship between digital
// values on the render target and the radiance levels shown by monitors.
// The DTF is part of the hardware. The DTF for most computers follow
// the sRGB color space spec.

// Hence, what we want is a "cancellation" of the effect of the DTF
// when encoding linear values to the render target. Hence, to remain
// a perceptually correct radiance level, by applying the "cancellation"
// or inverse of the DTF, which is called Gamma Correction.
// Gamma correction also means gamma encode and a non linear operation that the display applies to the signal
// is called gamma decode or gamma expansion.

// In particular, before writing the values for the frame buffer (colour
// returned by the pixel shader), a conversion must happen.

// To convert linear values to sRGB values, we simply raise the linear value
// by 1/2.2 (technically, there is a more accurate conversion that involves piecewise function).

// Images and textures must be converted to linear values from sRGB values for use.
// Hence, we simply raise the value by 2.2. At this point, the values are in linear space.
// Indeed, if we load a texture from a PNG or JPEG, these can be directly send to the framebuffer
// for display without conversion.

// This discussion implies that we need to feed the monitor the right values for display. We know that the
// monitor will raise our values with 2.2 (gamma expansion) to any energy we send to it.

// For instance, if our shader output is x, the monitor output is x^2.2, which is not what we want.
// hence, what we want to first raise x to 1/2.2 and the monitor output will be (x^(1/2.2))^(2.2) = x.

float4 srgb_to_linear(float4 c)
{
  return float4(pow(c.xyz, 2.2f), c.a);
}

float4 ps_main(VertexShader_Output ps_inp) : SV_Target
{
  float4 sample_colour     = ps_inp.colour;
  return sample_colour;
}
