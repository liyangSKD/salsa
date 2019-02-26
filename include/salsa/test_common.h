#pragma once
#include <gtest/gtest.h>
#include <cmath>
#include <string>
#include <fstream>
#include <yaml-cpp/yaml.h>

#define DEG2RAD (M_PI / 180.0)
#define RAD2DEG (180.0 / M_PI)

#define ASSERT_MAT_EQ(v1, v2) \
{ \
    ASSERT_EQ((v1).rows(), (v2).rows()); \
    ASSERT_EQ((v1).cols(), (v2).cols()); \
    for (int row = 0; row < (v1).rows(); row++) {\
        for (int col = 0; col < (v2).cols(); col++) {\
            ASSERT_FLOAT_EQ((v1)(row, col), (v2)(row,col));\
        }\
    }\
}

#define ASSERT_MAT_NEAR(v1, v2, tol) \
{ \
    ASSERT_EQ((v1).rows(), (v2).rows()); \
    ASSERT_EQ((v1).cols(), (v2).cols()); \
    for (int row = 0; row < (v1).rows(); row++) {\
        for (int col = 0; col < (v2).cols(); col++) {\
            ASSERT_NEAR((v1)(row, col), (v2)(row,col), (tol));\
        }\
    }\
}

#define EXPECT_MAT_NEAR(v1, v2, tol) \
{ \
    EXPECT_EQ((v1).rows(), (v2).rows()); \
    EXPECT_EQ((v1).cols(), (v2).cols()); \
    for (int row = 0; row < (v1).rows(); row++) {\
        for (int col = 0; col < (v2).cols(); col++) {\
            EXPECT_NEAR((v1)(row, col), (v2)(row,col), (tol));\
        }\
    }\
}

#define ASSERT_XFORM_NEAR(x1, x2, tol) \
{ \
    ASSERT_NEAR((x1).t()(0), (x2).t()(0), tol);\
    ASSERT_NEAR((x1).t()(1), (x2).t()(1), tol);\
    ASSERT_NEAR((x1).t()(2), (x2).t()(2), tol);\
    ASSERT_NEAR((x1).q().w(), (x2).q().w(), tol);\
    ASSERT_NEAR((x1).q().x(), (x2).q().x(), tol);\
    ASSERT_NEAR((x1).q().y(), (x2).q().y(), tol);\
    ASSERT_NEAR((x1).q().z(), (x2).q().z(), tol);\
}

#define ASSERT_QUAT_NEAR(q1, q2, tol) \
do { \
    Vector3d qt = (q1) - (q2);\
    ASSERT_LE(std::abs(qt(0)), tol);\
    ASSERT_LE(std::abs(qt(1)), tol);\
    ASSERT_LE(std::abs(qt(2)), tol);\
} while(0)

#define EXPECT_QUAT_NEAR(q1, q2, tol) \
do { \
    Vector3d qt = (q1) - (q2);\
    EXPECT_LE(std::abs(qt(0)), tol);\
    EXPECT_LE(std::abs(qt(1)), tol);\
    EXPECT_LE(std::abs(qt(2)), tol);\
} while(0)


#define EXPECT_MAT_FINITE(mat) \
do {\
  for (int c = 0; c < (mat).cols(); c++) \
  { \
    for (int r = 0; r < (mat).rows(); r++) \
    { \
      EXPECT_TRUE(std::isfinite((mat)(r,c))); \
    } \
  }\
} while(0)

#define EXPECT_MAT_NAN(mat) \
do {\
  for (int c = 0; c < (mat).cols(); c++) \
    { \
      for (int r = 0; r < (mat).rows(); r++) \
      { \
        EXPECT_TRUE(std::isnan((mat)(r,c))); \
      } \
  }\
} while(0)


#define EXPECT_FINITE(val) EXPECT_TRUE(std::isfinite(val))

inline std::string default_params()
{
    std::string filename = "/tmp/Salsa.default.yaml";
    std::ofstream tmp(filename);
    YAML::Node node;
    node["X_u2m"] = std::vector<double>{0, 0, 0, 1, 0, 0, 0};
    node["X_u2c"] = std::vector<double>{0, 0, 0, 1, 0, 0, 0};
    node["q_u2b"] = std::vector<double>{1, 0, 0, 0};
    node["tm"] = 0.0;
    node["tc"] = 0.0;
    node["log_prefix"] = "/tmp/Salsa/MocapSimulation/";
    node["R_clock_bias"] = std::vector<double>{1e-6, 1e-8};
    node["switch_weight"] = 10.0;
    node["acc_wander_weight"] = 0.003;
    node["gyro_wander_weight"] = 0.001;
    tmp << node;
    tmp.close();
    return filename;
}
