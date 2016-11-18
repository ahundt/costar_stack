/**************
 * This is a python binding for SemanticSegmentation
 * Felix Jonathan
 * 11/18/2016
**************/
#include <boost/python.hpp>
#include <boost/python/suite/indexing/vector_indexing_suite.hpp>

#include "sp_segmenter/semantic_segmentation.h"


using namespace boost::python;

BOOST_PYTHON_MODULE(ModelObjRecRANSACParameter)
{
    class_<ModelObjRecRANSACParameter>("ModelObjRecRANSACParameter")
        .def(init<double, double>())
        .def(init<double, double, double, double>())
        .def("setPairWidth", &ModelObjRecRANSACParameter::setPairWidth)
        .def("setVoxelSize", &ModelObjRecRANSACParameter::setVoxelSize)
        .def("setObjectVisibility", &ModelObjRecRANSACParameter::setObjectVisibility)
        .def("setSceneVisibility", &ModelObjRecRANSACParameter::setSceneVisibility)
    ;
}

BOOST_PYTHON_MODULE(objectTransformInformation)
{
    class_<objectTransformInformation>("objectTransformInformation")
        .def_readwrite("transform_name", &Var::transform_name_)
        .def_readwrite("model_name", &Var::model_name_)
        .def_readwrite("model_index", &Var::model_index_)
        .def_readwrite("model_name", &Var::model_name_)
    ;
}

enum ObjRecRansacMode {STANDARD_BEST, STANDARD_RECOGNIZE, GREEDY_RECOGNIZE};

struct objectTransformInformation
{
    // extended from poseT. All transform is with reference to the camera frame
    std::string transform_name_;
    std::string model_name_;
    unsigned int model_index_;
    Eigen::Vector3f origin_;
    Eigen::Quaternion<float> rotation_;
    double confidence_;

    objectTransformInformation() {};
    objectTransformInformation(const std::string &transform_name, const poseT &ObjRecRANSAC_result, unsigned const int &model_index) : transform_name_(transform_name), 
        model_name_(ObjRecRANSAC_result.model_name), model_index_(model_index),
        origin_(ObjRecRANSAC_result.shift), rotation_(ObjRecRANSAC_result.rotation), confidence_(ObjRecRANSAC_result.confidence)
    {};

    poseT asPoseT() const
    {
        poseT result;
        result.model_name = this->model_name_;
        result.shift = this->origin_;
        result.rotation = this->rotation_;
        result.confidence = this->confidence_;
        return result;
    }

    friend ostream& operator<<(ostream& os, const objectTransformInformation &tf_info);
};

class SemanticSegmentation
{
public:
    SemanticSegmentation();
    ~SemanticSegmentation();
// ---------------------------------------------------------- MAIN OPERATIONAL FUNCTIONS --------------------------------------------------------------------------------------------

    // initializeSemanticSegmentation needs to be called after all main parameter has been set. It will check whether all parameters has been set properly or not
    void initializeSemanticSegmentation();

    // segment input point cloud pcl::PointXYZRGBA to labelled point cloud pcl::PointXYZL
    bool segmentPointCloud(const pcl::PointCloud<pcl::PointXYZRGBA>::Ptr &input_cloud, pcl::PointCloud<pcl::PointXYZL>::Ptr &result);
#ifdef USE_OBJRECRANSAC
    // calculate all object poses based on the input labelled point cloud generated from segmentPointCloud function.
    std::vector<objectTransformInformation> calculateObjTransform(const pcl::PointCloud<pcl::PointXYZL>::Ptr &labelled_point_cloud);

    // segment and calculate all object poses based on input cloud. It returns true if the segmentation successful and the detected object poses > 0
    bool segmentAndCalculateObjTransform(const pcl::PointCloud<pcl::PointXYZRGBA>::Ptr &input_cloud, 
        pcl::PointCloud<pcl::PointXYZL>::Ptr &labelled_point_cloud_result, std::vector<objectTransformInformation> &object_transform_result);

    // Update one object pose that has matching transform name and object type, then returns that updated pose with other poses(from previous detection). 
    std::vector<objectTransformInformation> getUpdateOnOneObjTransform(const pcl::PointCloud<pcl::PointXYZL>::Ptr &labelled_point_cloud, const std::string &transform_name, const std::string &object_type);
#endif

// ---------------------------------------------------------- ADDITIONAL OPERATIONAL FUNCTIONS --------------------------------------------------------------------------------------
    bool getTableSurfaceFromPointCloud(const pcl::PointCloud<pcl::PointXYZRGBA>::Ptr &input_cloud, const bool &save_table_pcd = false, const std::string &save_directory_path = ".");
    void convertPointCloudLabelToRGBA(const pcl::PointCloud<pcl::PointXYZL>::Ptr &input, pcl::PointCloud<pcl::PointXYZRGBA>::Ptr &output) const;
// --------------------------------- MAIN PARAMETERS for point cloud segmentation that needs to be set before initializeSemanticSegmentation ----------------------------------------
    void setDirectorySHOT(const std::string &path_to_shot_directory);
    void setDirectorySVM(const std::string &path_to_svm_directory);
    void setDirectorySVM(const std::string &path_to_svm_directory, const bool &use_binary_svm, const bool &use_multi_class_svm);
    void setUseMultiClassSVM(const bool &use_multi_class_svm);
    void setUseBinarySVM(const bool &use_binary_svm);
    template <typename NumericType>
        void setPointCloudDownsampleValue(const NumericType &down_ss);
    template <typename NumericType>
        void setHierFeaRatio(const NumericType &ratio);

// --------------------------------- MAIN PARAMETERS for ObjRecRANSAC that needs to be set before initializeSemanticSegmentation if compute pose is used-------------------------------
    void setUseComputePose(const bool &compute_pose);
    void setUseCuda(const bool &use_cuda);
    void setModeObjRecRANSAC(const int &mode);
    template <typename NumericType>
        void setMinConfidenceObjRecRANSAC(const NumericType &min_confidence);
#ifdef USE_OBJRECRANSAC
    void addModel(const std::string &path_to_model_directory, const std::string &model_name, const ModelObjRecRANSACParameter &parameter);
#endif

// --------------------------------- OPTIONAL PARAMETERS that does not need to be set before initializeSemanticSegmentation, but may help in some cases -------------------------------
    // Visualize the point cloud segmentation with pcl visualizer
    void setUseVisualization(const bool &visualization_flag);

    // Crop box will crop the point cloud by making a box with crop_box_size_ around the crop_box_pose. It can be used to remove background data without using binarySVM
    void setUseCropBox(const bool &use_crop_box);
    template <typename NumericType>
        void setCropBoxSize(const NumericType &x, const NumericType &y, const NumericType &z);
    template <typename NumericType>
        void setCropBoxSize(const Eigen::Matrix<NumericType, 3, 1> &crop_box_size);
    template <typename NumericType>
        void setCropBoxPose(const Eigen::Transform< NumericType, 3, Eigen::Affine> &target_pose_relative_to_camera_frame);

    // Table Segmentation will extract the points above the table region. It can be used to remove background data without using binarySVM
    void setUseTableSegmentation(const bool &use_table_segmentation);
    template <typename NumericType>
        void setCropAboveTableBoundary(const NumericType &min, const NumericType &max);
    void loadTableFromFile(const std::string &table_pcd_path);
    template <typename NumericType1, typename NumericType2, typename NumericType3>
        void setTableSegmentationParameters(const NumericType1 &table_distance_threshold,const NumericType2 &table_angular_threshold,const NumericType3 &table_minimal_inliers);

    // Symmetric parameters will post-process ObjRecRANSAC pose to achieve more consistent pose for all objects that has symmetric properties (e.g. boxes)
    // by returning a pose that is closest to a preferred orientation or identity (if the preferred orientation is not set). If the symmetric parameter is not set, the object assumed to have no symmetric property
    template <typename NumericType>
        void addModelSymmetricProperty(const std::string &model_name, const NumericType &roll, const NumericType &pitch, const NumericType &yaw, const NumericType &step, const std::string &preferred_axis);
    void addModelSymmetricProperty(const std::map<std::string, objectSymmetry> &object_dict);
    void setUsePreferredOrientation(const bool &use_preferred_orientation);
    template <typename NumericType>
        void setPreferredOrientation(const Eigen::Quaternion<NumericType> &base_rotation);

    // Object persistence will post-process ObjRecRANSAC pose to get an object orientation that is closest to the previous detection, 
    // if the detected object position is within 2.5 cm compared to previous object position
    void setUseObjectPersistence(const bool &use_object_persistence);

protected:
    void cropPointCloud(pcl::PointCloud<PointT>::Ptr &cloud_input, 
        const Eigen::Affine3f &camera_transform_in_target, 
        const Eigen::Vector3f &box_size) const;
    std::vector<objectTransformInformation> getTransformInformationFromTree() const;
    bool checkFolderExist(const std::string &directory_path) const;
    bool class_ready_;
    bool visualizer_flag_;

    // Point Cloud Modifier Parameter
    Eigen::Vector3f crop_box_size_;
    bool use_crop_box_;
    Eigen::Affine3f crop_box_target_pose_;

    // Training File Informations
    bool svm_loaded_, shot_loaded_;
    bool use_binary_svm_, use_multi_class_svm_;
    std::vector<model*> binary_models_, multi_models_;
    std::vector<ModelT> mesh_set_;
    std::map<std::string, std::size_t> model_name_map_;
    std::size_t number_of_added_models_;

    // Table Parameters
    bool have_table_, use_table_segmentation_;
    double table_distance_threshold_, table_angular_threshold_;
    unsigned int table_minimal_inliers_;
    double above_table_min, above_table_max;
    pcl::PointCloud<PointT>::Ptr table_corner_points_;

    // Point Cloud Segmentation Parameters
    float pcl_downsample_;
    float hier_radius_, hier_ratio_;
    bool compute_pose_;
    bool use_cuda_;

    // ObjRecRANSAC Parameters
    int objRecRANSAC_mode_;
    double min_objrecransac_confidence;

    // Object Transformation Parameter
    bool use_preferred_orientation_;
    Eigen::Quaternion<double> base_rotation_;
    bool use_object_persistence_;

    std::map<std::string, objectSymmetry> object_dict_;
    // keep information about TF index
    std::map<std::string, unsigned int> object_class_transform_index_;
#ifdef USE_OBJRECRANSAC
    boost::shared_ptr<greedyObjRansac> combined_ObjRecRANSAC_;
    std::vector<boost::shared_ptr<greedyObjRansac> > individual_ObjRecRANSAC_;

    // map of symmetries for orientation normalization
    objectRtree segmented_object_tree_;
#endif
    pcl::visualization::PCLVisualizer::Ptr viewer;
    Hier_Pooler hie_producer;
    std::vector< boost::shared_ptr<Pooler_L0> > lab_pooler_set;
    
    uchar color_label[11][3];
};

// template function implementation
#include "sp_segmenter/semantic_segmentation.tcc"
#endif
