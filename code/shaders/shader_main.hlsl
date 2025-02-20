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
  uint       enable_texture;
  uint       enable_reflections;
  float      _pad_c1_a[2];
};

cbuffer Constant_Store2 : register(b2)
{
  float3     eye_p;
  uint       light_count;
  Light      lights[MaxLightCount];
};

struct Model_Instance
{
  float3x3 model_to_world_xform;
  float3x3 model_to_world_xform_inverse_transpose;
  float3 p;
  float4 colour;
  uint enable_lighting;
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

  nointerpolation uint enable_lighting    : EnableLighting;
  float3 world_p                          : WorldP;
  float3 normal                           : SurfaceNormal;
  
  float3x3 TBN_to_world         : TBNToWorld;
};

StructuredBuffer<Model_Instance>   g_model_instances   : register(t0);
Texture2D<float4>                  g_diffuse_map       : register(t1);
Texture2D<float4>                  g_normal_map        : register(t2);
Texture2D<float4>                  g_displace_map      : register(t3);

SamplerState g_sample_linear_all : register(s0);

VertexShader_Output
vs_main(VertexShader_Input vs_inp, uint iid : SV_InstanceID)
{
  Model_Instance instance = g_model_instances[iid];
  
  // assumptions: vs_inp.t, vs_inp.b, and vs_inp.n is a basis
  // basis of R^3. The world coordinate system is standard basis.
  float3 T = mul(instance.model_to_world_xform_inverse_transpose, vs_inp.t);
  float3 B = mul(instance.model_to_world_xform_inverse_transpose, vs_inp.b);
  float3 N = mul(instance.model_to_world_xform_inverse_transpose, vs_inp.n);
  
  B = B - ((dot(B, T) / dot(T, T)) * T);
  N = N - ((dot(N, T) / dot(T, T)) * T) - ((dot(N, B) / dot(B, B)) * B);
  
  T = normalize(T);
  B = normalize(B);
  N = normalize(N);
  
  VertexShader_Output result = (VertexShader_Output)0;

  float3 world_p          = mul(instance.model_to_world_xform, vs_inp.p) + instance.p;
  float4 camera_p         = mul(world_basis_to_camera_basis, float4(world_p, 1.0f));
  
  // Matrix of an identity map from the TBN basis to World Basis  
  float3x3 TBN_to_world = float3x3(T.x, B.x, N.x,
                                   T.y, B.y, N.y,
                                   T.z, B.z, N.z);
  // Matrix of a linear map from World Basis to World Basis
  //result.TBN_to_world_t = mul(instance.model_to_world_xform_inverse_transpose, TBN_to_world);
  result.TBN_to_world   = TBN_to_world;

  result.p         = mul(projection, camera_p);
  result.colour    = instance.colour;
  result.uv        = vs_inp.uv;
  
  result.world_p         = world_p;
  result.normal          = mul(instance.model_to_world_xform_inverse_transpose, vs_inp.n);
  result.enable_lighting = instance.enable_lighting;
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

float2 parallax_uv(float3 view_dir, float3 N, float2 tex_coord, float2 dx, float2 dy)
{
  float  height_scale_tweak        = 0.05f;
  uint   sample_count_min_tweak    = 8;
  uint   sample_count_max_tweak    = 32;
  float  sample_count              = 32;//lerp((float)sample_count_max_tweak, (float)sample_count_min_tweak, max(dot(view_dir, N), 0.0f));
  
  float  depth_sample_step         = 1.0f / sample_count;
  float2 largest_parallax_offset   = (view_dir.xy / view_dir.z) * (-height_scale_tweak);
  float2 tex_sample_step           = largest_parallax_offset / sample_count;
  tex_sample_step.y               *= -1.0f;
  
  float  current_sample_depth      = 0.0f;
  float2 current_tex_coords        = tex_coord;
  float  current_depth_map_value   = 1.0f - g_displace_map.SampleGrad(g_sample_linear_all, current_tex_coords, dx, dy).r;
  
  while (current_sample_depth < current_depth_map_value)
  {
    current_tex_coords       += tex_sample_step;
    current_depth_map_value   = 1.0f - g_displace_map.SampleGrad(g_sample_linear_all, current_tex_coords, dx, dy).r;
    current_sample_depth     += depth_sample_step;
  }
  
  float2 tex_coord_before        = current_tex_coords - tex_sample_step;
  float  depth_after             = current_depth_map_value - current_sample_depth;
  float  prev_depth_map_value    = 1.0f - g_displace_map.SampleGrad(g_sample_linear_all, tex_coord_before, dx, dy).r;
  float  depth_before            = prev_depth_map_value - current_sample_depth + depth_sample_step;
  float  t_value                 = depth_after / (depth_after - depth_before);
  float2 result                  = float2(t_value * tex_coord_before + (1.0f - t_value) * current_tex_coords);
  return result;
}

float4 ps_main(VertexShader_Output ps_inp) : SV_Target
{
  float4 sample_colour     = ps_inp.colour;
  float3 N                 = normalize(ps_inp.normal);
  float3 to_eye            = normalize(eye_p - ps_inp.world_p);
  
  if (enable_texture)
  {
    float3x3 world_to_TBN    = transpose(ps_inp.TBN_to_world);
    float3   TBN_E           = mul(world_to_TBN, to_eye);
    float3   TBN_N           = mul(world_to_TBN, N);
    
    float2 dx                = ddx(ps_inp.uv);
    float2 dy                = ddy(ps_inp.uv);
    float2 tex_coord_tweak   = parallax_uv(TBN_E, TBN_N, ps_inp.uv, dx, dy);
    
    if (((tex_coord_tweak.x > 1.0f) || (tex_coord_tweak.x < 0.0f)) ||
        ((tex_coord_tweak.y > 1.0f) || (tex_coord_tweak.y < 0.0f)))
    {
      discard;
    }

    
    float4 texel             = g_diffuse_map.SampleGrad(g_sample_linear_all, tex_coord_tweak, dx, dy);
    sample_colour           *= texel;
    
    //return g_normal_map.SampleGrad(g_sample_linear_all, tex_coord_tweak, dx, dy);
    N = g_normal_map.SampleGrad(g_sample_linear_all, tex_coord_tweak, dx, dy).xyz * 2.0f - 1.0f;
    N = normalize(mul(ps_inp.TBN_to_world, N));
  }

  float M_diffuse     = 1.0f;
  float M_specular    = 0.5f;
  float M_ambient     = 1.0f;
  float M_shininess   = 8.0f;
  
  float4 final_colour = 0;
  if (ps_inp.enable_lighting)
  {
    [unroll]
    for (uint light_idx = 0; light_idx < light_count; ++light_idx)
    {
      Light light = lights[light_idx];
      
      switch (light.type)
      {
        case LightType_Directional:
        {
          float3 L          = normalize(light.dir);
          float3 R          = normalize(reflect(L, N));
          float3 H          = normalize(-L + to_eye);
          float n_dot_l     = max(dot(N, -L), 0.0f);
          //float r_dot_v     = max(dot(to_eye, R), 0.0f);
          float r_dot_v     = max(dot(N, H), 0.0f);
          
          float4 diffuse    = M_diffuse * light.intensity * n_dot_l * sample_colour;
          float4 specular   = M_specular * pow(r_dot_v, M_shininess) * light.intensity;
          
          final_colour      = saturate(diffuse + specular + final_colour);
          //sample_colour.xyz *= n_dot_l;
        } break;
        
        case LightType_Point:
        {
          float3 L        = light.P - ps_inp.world_p;
          float  dist     = length(L);
          L              /= dist;        
          float r0        = 5.0f;
          float rmax      = 50.0f;
          float win       = pow(max(1.0f - pow(dist / rmax, 4.0f), 0.0f), 2.0f);
          float atten     = win*(r0*r0 / (dist*dist + 0.01f));
          
          float3 R        = normalize(reflect(-L, N));
          float3 H          = normalize(L + to_eye);
          
          float n_dot_l     = max(dot(N, L), 0.0f);
          float r_dot_v     = max(dot(N, H), 0.0f);
          
          float4 diffuse    = M_diffuse * light.intensity * n_dot_l * sample_colour;
          float4 specular   = M_specular * pow(r_dot_v, M_shininess) * light.intensity;
          
          final_colour      = saturate((diffuse + specular)*atten + final_colour);
        } break;
      }
    }
  }
  else
  {
    final_colour = sample_colour;
  }
  
  final_colour = saturate(M_ambient * float4(0.1f, 0.1f, 0.1f, 1.0f) * sample_colour + final_colour);
  return final_colour;
}