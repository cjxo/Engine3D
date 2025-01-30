#include <math.h>

static f32
v3f_inner(v3f a, v3f b)
{
  f32 result = a.x*b.x + a.y*b.y + a.z*b.z;
  return(result);
}

static v3f
v3f_cross(v3f a, v3f b)
{
  v3f result;
  result.x = a.y * b.z - a.z * b.y;
  result.y = -(a.x * b.z - a.z * b.x);
  result.z = a.x * b.y - a.y * b.x;
  return(result);
}

static v3f
v3f_scale(f32 a, v3f b)
{
  v3f result = (v3f) {
    a*b.x,
    a*b.y,
    a*b.z,
  };
  return(result);
}

static v3f
v3f_normalized(v3f a)
{
  f32 ilen = 1.0f / sqrtf(a.x*a.x + a.y*a.y + a.z*a.z);
  v3f result = (v3f) {
    a.x*ilen, 
    a.y*ilen, 
    a.z*ilen, 
  };

  return(result);
}

static v3f
v3f_sub(v3f a, v3f b)
{
  v3f result = (v3f) {
    a.x - b.x,
    a.y - b.y,
    a.z - b.z,
  };

  return(result);
}

static void
v3f_sub_eq(v3f *a, v3f b)
{
  a->x -= b.x;
  a->y -= b.y;
  a->z -= b.z;
}

static void
v3f_add_eq(v3f *a, v3f b)
{
  a->x += b.x;
  a->y += b.y;
  a->z += b.z;
}

static Basis_R3
br3_from_center_to_target(v3f center, v3f target, v3f temp_up)
{
  v3f front = v3f_normalized(v3f_sub(target, center));
  v3f up    = v3f_normalized(v3f_sub(temp_up, v3f_scale(v3f_inner(temp_up, front), front)));
  v3f right = v3f_normalized(v3f_cross(up, front));

  return (Basis_R3) {
    .x = right,
    .y = up,
    .z = front,
  };
}

static m33
m33_make_identity(void)
{
  m33 result = { 0 };
  result.m[0][0] = 1.0f;
  result.m[1][1] = 1.0f;
  result.m[2][2] = 1.0f;
  return(result);
}

static m33
m33_make_diag(v3f d)
{
  m33 result = { 0 };
  result.m[0][0] = d.x;
  result.m[1][1] = d.y;
  result.m[2][2] = d.z;
  return(result);
}

static m33
m33_mul(m33 a, m33 b)
{
  m33 result = { 0 };
  for (u32 row = 0; row < 3; ++row)
  {
    for (u32 col = 0; col < 3; ++col)
    {
      for (u32 dot = 0; dot < 3; ++dot)
      {
        result.m[row][col] += a.m[row][dot] * b.m[dot][col];
      }
    }
  }

  return(result);
}

static m33
m33_make_rot_xz(f32 rot_rad)
{
  f32 c = cosf(rot_rad);
  f32 s = sinf(rot_rad);
  m33 result = (m33) {
    c, 0.0f, s,
    0.0f, 1, 0,
    -s, 0, c,
  };
  return(result);
}

static m33
m33_make_rot_yz(f32 rot_rad)
{
  f32 c = cosf(rot_rad);
  f32 s = sinf(rot_rad);
  m33 result = (m33) {
    1.0f, 0.0f, 0.0f,
    0.0f, c, -s,
    0.0f, s, c,
  };
  return(result);
}

static m33
m33_make_rot_xy(f32 rot_rad)
{
  f32 c = cosf(rot_rad);
  f32 s = sinf(rot_rad);
  m33 result = (m33) {
    c, -s, 0.0f,
    s, c, 0.0f,
    0.0f, 0.0f, 1.0f
  };
  return(result);
}

static v3f
m33_mul_v3f(m33 a, v3f b)
{
  v3f result =
  {
    v3f_inner(a.r[0], b),
    v3f_inner(a.r[1], b),
    v3f_inner(a.r[2], b),
  };

  return(result);
}

static m44
m44_make_perspective_z01(f32 aspect_height_over_width, f32 fov_radians, f32 near_plane, f32 far_plane)
{
  f32 right   = tanf(fov_radians * 0.5f) * near_plane;
  f32 left    = -right;
  f32 top     = aspect_height_over_width * right;
  f32 bottom  = -top;

  m44 result;

  result.r[0] = (v4f){ (2.0f * near_plane) / (right - left), 0.0f, 0.0f, 0.0f };
  
  result.r[1] = (v4f){ 0.0f, (2.0f * near_plane) / (top - bottom), 0.0f, 0.0f };
  
  result.r[2] = (v4f){ -(right + left) / (right - left),         -(top + bottom) / (top - bottom), far_plane / (far_plane - near_plane), 1.0f };
  
  result.r[3] = (v4f){ 0.0f, 0.0f, (-near_plane * far_plane) / (far_plane - near_plane), 0.0f };

  return(result);
}

static m44
m44_make_orthographic_z01(f32 left, f32 right, f32 bottom, f32 top, f32 near_plane, f32 far_plane)
{
  m44 result;
  result.r[0] = (v4f){           2.0f / (right - left),                             0.0f,                 0.0f, 0.0f};
  result.r[1] = (v4f){                               0,            2.0f / (top - bottom),                 0.0f, 0.0f};
  result.r[2] = (v4f){                               0,                             0.0f,  1.0f / (far_plane - near_plane), 0.0f};
  result.r[3] = (v4f){-(right + left) / (right - left), -(top + bottom) / (top - bottom), -near_plane / (far_plane - near_plane), 1.0f};
  return(result);
}

static m44
m44_mul(m44 a, m44 b)
{
  m44 result = { 0 };
  for (u32 row = 0; row < 4; ++row)
  {
    for (u32 col = 0; col < 4; ++col)
    {
      for (u32 dot = 0; dot < 4; ++dot)
      {
        result.m[row][col] += a.m[row][dot] * b.m[dot][col];
      }
    }
  }

  return(result);
}
