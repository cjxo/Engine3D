#if !defined(MY_MATH_H)
#define MY_MATH_H

#define PIF32 3.14159f
#define Radians(deg) ((deg)*(3.14159f/180.0f))

typedef union
{
  struct
  {
    f32 x, y;
  };

  f32 v[2];
} v2f;

typedef union
{
  struct
  {
    f32 x, y, z;
  };

  struct
  {
    f32 r, g, b;
  };

  f32 v[3];
} v3f;

#define v3f_zero() (v3f){0.0f,0.0f,0.0f}
static f32  v3f_inner(v3f a, v3f b);
static v3f  v3f_cross(v3f a, v3f b);
static v3f  v3f_scale(f32 a, v3f b);
static v3f  v3f_normalized(v3f a);
static v3f  v3f_sub(v3f a, v3f b);
static void v3f_sub_eq(v3f *a, v3f b);
static void v3f_add_eq(v3f *a, v3f b);

typedef struct
{
  v3f x, y, z;
} Basis_R3;

static Basis_R3 br3_from_center_to_target(v3f center, v3f target, v3f temp_up);

typedef union
{
  f32 m[3][3];
  v3f r[3];
} m33;

static m33 m33_make_identity(void);
static m33 m33_make_diag(v3f d);
static m33 m33_make_rot_xz(f32 rot_rad);
static m33 m33_make_rot_yz(f32 rot_rad);
static m33 m33_make_rot_xy(f32 rot_rad);
static m33 m33_mul(m33 a, m33 b);
static v3f m33_mul_v3f(m33 a, v3f b);

typedef union
{
  struct
  {
    f32 x, y, z, w;
  };

  struct
  {
    f32 r, g, b, a;
  };

  f32 v[4];
} v4f;

typedef union
{
  f32 m[4][4];
  v4f r[4];
} m44;

static m44 m44_make_perspective_z01(f32 aspect_height_over_width, f32 fov_radians, f32 near_plane, f32 far_plane);
static m44 m44_make_orthographic_z01(f32 left, f32 right, f32 bottom, f32 top, f32 near_plane, f32 far_plane);
static m44 m44_mul(m44 a, m44 b);

#endif
