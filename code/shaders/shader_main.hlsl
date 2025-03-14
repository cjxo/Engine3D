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
Texture2D<float4>                  g_shadow_map        : register(t4);

SamplerState g_sample_linear_all : register(s0);
SamplerState g_sample_point_all  : register(s1);
SamplerComparisonState sampler_shadow : register(s2);

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

float4
vs_depth_only(VertexShader_Input vs_inp, uint iid : SV_InstanceID) : SV_Position
{
  Model_Instance instance = g_model_instances[iid];
  if ((instance.p.x == lights[0].P.x) && (instance.p.y == lights[0].P.y) && (instance.p.z == lights[0].P.z))
  {
    return float4(0,0,0,0);
  }
  float3 world_p          = mul(instance.model_to_world_xform, vs_inp.p) + instance.p;
  float4 camera_p         = mul(lights[0].world_to_light, float4(world_p, 1.0f));
  float4 result           = mul(lights[0].projection, camera_p);
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
  uint   sample_count_max_tweak    = 64;
  float  sample_count              = (float)lerp((float)sample_count_max_tweak, (float)sample_count_min_tweak, dot(view_dir, -N));
  
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

float2 parallax_uv2(float3 view_dir, float3 N, float2 tex_coord, float2 dx, float2 dy)
{
  float  height_scale_tweak     = 0.04f;
  uint   min_sample_count_tweak = 8;
  uint   max_sample_count_tweak = 48;
  
  float  sample_countf           = (float)lerp((float)max_sample_count_tweak, (float)min_sample_count_tweak, max(dot(view_dir, -N), 0.0f));
  uint   sample_count            = (uint)sample_countf;
  
  float2 max_parallax_offset = ((-height_scale_tweak) * view_dir.xy) / view_dir.z;
  float  depth_step          = 1.0f / sample_countf;
  float2 tex_step            = max_parallax_offset / sample_countf;
  tex_step.y                *= -1.0f;
  
  uint   sample_idx              = 0;
  float2 current_tex_offset      = 0.0f;
  float2 prev_tex_offset         = 0.0f;
  float  current_depth           = 1.0f - depth_step;
  float  previous_depth          = 1.0f;
  float  current_map_depth       = 0.0f;
  float  previous_map_depth      = 0.0f;
  float2 final_tex_offset        = 0.0f;  
  while (sample_idx <= sample_count)
  {
    current_map_depth = g_displace_map.SampleGrad(g_sample_linear_all, tex_coord + current_tex_offset, dx, dy).r;
    
    if (current_depth < current_map_depth)
    {
      float t             = (previous_map_depth - previous_depth) / (current_depth - previous_depth - current_map_depth + previous_map_depth);
      final_tex_offset    = prev_tex_offset + t * tex_step;
      
      sample_idx = sample_count + 1;
    }
    else
    {
      ++sample_idx;
      
      prev_tex_offset       = current_tex_offset;
      previous_depth        = current_depth;
      previous_map_depth    = current_map_depth;
      
      current_tex_offset   += tex_step;
      current_depth        -= depth_step;
    }
  }
  
  return tex_coord + final_tex_offset;
}

float4 ps_main(VertexShader_Output ps_inp) : SV_Target
{
  float4 sample_colour     = ps_inp.colour;
  float3 N                 = normalize(ps_inp.normal);
  float3 to_eye            = normalize(eye_p - ps_inp.world_p);
  
  if (enable_texture)
  {
    float3x3 world_to_TBN    = transpose(ps_inp.TBN_to_world);
    float3   TBN_E           = normalize(mul(world_to_TBN, to_eye));
    float3   TBN_N           = normalize(mul(world_to_TBN, N));
    
    float2 dx                = ddx(ps_inp.uv);
    float2 dy                = ddy(ps_inp.uv);
    float2 tex_coord_tweak   = parallax_uv2(TBN_E, TBN_N, ps_inp.uv, dx, dy);
    
    float4 texel             = g_diffuse_map.SampleGrad(g_sample_linear_all, tex_coord_tweak, dx, dy);
    sample_colour           *= texel;
    
    //return g_normal_map.SampleGrad(g_sample_linear_all, tex_coord_tweak, dx, dy);
    N = g_normal_map.SampleGrad(g_sample_linear_all, tex_coord_tweak, dx, dy).xyz * 2.0f - 1.0f;
    N = normalize(mul(ps_inp.TBN_to_world, N));
  }

  float shadow_multiplier = 0.0f;
  {
    Light light         = lights[0];
    float4 light_p      = mul(light.world_to_light, float4(ps_inp.world_p, 1.0f));
    light_p             = mul(light.projection, light_p);
    light_p.xyz        /= light_p.w;

    if (light_p.z > 1)
    {
      shadow_multiplier = 1.0f;
    }
    else
    {
      float  bias           = max(0.005f * (1.0f - dot(N, -light.dir)), 0.0005f);
      float  current_depth  = light_p.z - bias;
      float2 shadow_tex_p   = float2(light_p.x * 0.5f + 0.5f, 1.0f - (light_p.y * 0.5f + 0.5f));
      float2 texel_size     = 1.0f / 1024.0f;
      float2 texel_p        = shadow_tex_p * 1024.0f;
      //float4 subpixel       = float4(frac(texel_p), 1.0f - frac(texel_p));
      //float4 bilinear       = subpixel.zxzx * subpixel.wwyy;
      float2 t              = frac(texel_p);
      
      [unroll]  
      for(int x = -1; x <= 1; ++x)
      {
        [unroll]  
        for(int y = -1; y <= 1; ++y)
        {
          float2 uv            = shadow_tex_p + float2(x, y) * texel_size;
          float s0             = g_shadow_map.Sample(g_sample_point_all, uv).r;
          float s1             = g_shadow_map.Sample(g_sample_point_all, uv + float2(texel_size.x, 0.0f)).r;
          float s2             = g_shadow_map.Sample(g_sample_point_all, uv + float2(0.0f, texel_size.y)).r;
          float s3             = g_shadow_map.Sample(g_sample_point_all, uv + texel_size).r;
          float4 tests         = (current_depth < float4(s0, s1, s2, s3)) ? 1.0f : 0.4f;
          //shadow_multiplier   += dot(bilinear, tests);
	  shadow_multiplier   += lerp(lerp(tests.x, tests.y, t.x), lerp(tests.z, tests.w, t.x), t.y);
        }
      }

      shadow_multiplier /= 9.0f;
    }
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
          
          final_colour      = saturate((diffuse + specular) * shadow_multiplier + final_colour);
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
