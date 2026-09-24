/* DBSCAN 聚类参数 */
#define DBSCAN_WEIGHT_X         0.1f   /* 纵向权重(极低): 容忍同护栏延伸 */
#define DBSCAN_WEIGHT_Y         2.0f    /* 横向权重(高): 区分不同护栏 */

/* RANSAC 拟合参数 */
#define RANSAC_SAMPLE_SIZE      3
#define RANSAC_MAX_DISTANCE     1.0f
#define RANSAC_MAX_TRIALS       9999

/* 关联阈值 */
#define TH_MAX_X_DIFF_UPDATE    30.0f   /* 更新时x外扩 */
#define TH_MAX_X_DIFF_MARK      5.0f    /* 标记/删除时x外扩 */
#define TH_MAX_ASSOC_DIST       1.5f

/* 生命周期 */
#define FORGET_COEFF_RLS        1.0f
#define MAX_MISS_CNT            6
#define EMA_ALPHA_X_RANGE       0.2f

/* 形状判别 */
#define MIN_LONGITUDINAL_SPAN   15.0f   /* 最小纵向跨度(m) */
#define MIN_ASPECT_RATIO        3.0f    /* 纵/横最小比例 */
#define MAX_X_RANGE_STATIC_ZONE 50.0f  /* 处理的最大纵向距离(m) */

/* 下采样 */
#define TH_DOWNSAMPLE_TOL       2.0f

/* 模型参数数量 (直线: y = kx + b) */
#define LINE_MODEL_SIZE         2

void downsample_points(float* points, int* numPoints, float tol);

int static_zone_cluster_direct(const StaticDetInfo* repo, unsigned char* clusterIDs);

void reset_static_zone(StaticZoneObj* zone);

bool check_strip_shape(const float* points, int numPoints);

void static_zone_mark_detections(GlbCtx* ctx);