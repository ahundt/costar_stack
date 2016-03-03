#include <opencv2/core/core.hpp>
#include <opencv2/features2d/features2d.hpp>
#include <opencv2/highgui/highgui.hpp>
#include "sp_segmenter/features.h"
#include "sp_segmenter/JHUDataParser.h"
#include "sp_segmenter/greedyObjRansac.h"
#include "sp_segmenter/plane.h"
#include "sp_segmenter/refinePoses.h"
#include "sp_segmenter/common.h"
#include "sp_segmenter/stringVectorArgsReader.h"

// ros stuff
#include <ros/ros.h>
#include <sensor_msgs/PointCloud2.h>
#include <geometry_msgs/Pose.h>
#include <geometry_msgs/PoseArray.h>

// for TF services
#include <std_srvs/Empty.h>
// #include <tf/tf_conversions.h>
#include <tf/transform_broadcaster.h>
#include <tf/transform_listener.h>

// include to convert from messages to pointclouds and vice versa
#include <pcl_conversions/pcl_conversions.h>

// chi objrec ransac utils
#include <eigen3/Eigen/src/Geometry/Quaternion.h>

// contains function to normalize the orientation of symmetric object
#include "sp_segmenter/symmetricOrientationRealignment.h"
#include "sp_segmenter/table_segmenter.h"

// ros service messages for segmenting gripper
#include "sp_segmenter/segmentInGripper.h"
#include "sp_segmenter/segmenterTFObject.h"

#define OBJECT_MAX 100

std::map<std::string, segmentedObjectTF> segmentedObjectTFMap;
std::map<std::string, unsigned int> objectTFIndex;


//for TF
bool hasTF;

sensor_msgs::PointCloud2 inputCloud;

// for orientation normalization
std::map<std::string, objectSymmetry> objectDict;

pcl::PointCloud<PointT>::Ptr tableConvexHull(new pcl::PointCloud<PointT>);

bool compute_pose = false;
bool view_flag = false;
pcl::visualization::PCLVisualizer::Ptr viewer;
Hier_Pooler hie_producer;
std::vector< boost::shared_ptr<Pooler_L0> > lab_pooler_set(5+1);
std::vector<model*> binary_models(3);
float maxRegion[3],minRegion[3];
float radius = 0.02;
float down_ss = 0.003;
double aboveTable;
double pairWidth = 0.05;
double voxelSize = 0.003; 
bool bestPoseOnly;
double minConfidence;

tf::TransformListener * listener;

boost::shared_ptr<greedyObjRansac> objrec;
std::vector<std::string> model_name(OBJECT_MAX, "");
std::vector<ModelT> mesh_set;

std::string POINTS_IN, POINTS_OUT, POSES_OUT;
std::string gripperTF;

ros::Publisher pc_pub;
// ros::Publisher pose_pub;
ros::Subscriber pc_sub;

std::map<std::string, int> model_name_map;
uchar color_label[11][3] = 
{ {0, 0, 0}, 
  {255, 0, 0},
  {0, 255, 0},
  {0, 0, 255},
  {255, 255, 0},
  {255, 0, 255},
  {0, 255, 255},
  {255, 128, 0},
  {255, 0, 128},
  {0, 128, 255},
  {128, 0, 255},
};

void visualizeLabels(const pcl::PointCloud<PointLT>::Ptr label_cloud, pcl::visualization::PCLVisualizer::Ptr viewer, uchar colors[][3]);
pcl::PointCloud<PointLT>::Ptr densifyLabels(const pcl::PointCloud<PointLT>::Ptr label_cloud, const pcl::PointCloud<PointT>::Ptr ori_cloud);

/*********************************************************************************/

std::vector<poseT> spSegmenterCallback(
    const pcl::PointCloud<PointT>::Ptr full_cloud, pcl::PointCloud<PointLT> & final_cloud)
{
    // By default pcl::PassThrough will remove NaNs point unless setKeepOrganized is true
    pcl::PassThrough<PointT> pass;
    pcl::PointCloud<PointT>::Ptr scene_f(new pcl::PointCloud<PointT>());
    *scene_f = *full_cloud;
        
    if( viewer )
    {
        std::cout<<"VISUALIZING"<<std::endl;
        viewer->removeAllPointClouds();
        viewer->addPointCloud(scene_f, "whole_scene");
        viewer->spin();
        viewer->removeAllPointClouds();
    }
    //pcl::PointCloud<PointT>::Ptr scene_f = removePlane(scene,aboveTable);

    // pcl::PointCloud<NormalT>::Ptr cloud_normal(new pcl::PointCloud<NormalT>());
    //computeNormals(cloud, cloud_normal, radius);
    //pcl::PointCloud<PointT>::Ptr label_cloud = recog.recognize(cloud, cloud_normal);

    spPooler triple_pooler;
    triple_pooler.lightInit(scene_f, hie_producer, radius, down_ss);
    std::cerr << "LAB Pooling!" << std::endl;
    triple_pooler.build_SP_LAB(lab_pooler_set, false);

    if(viewer)
    {
      viewer->removeAllPointClouds();
    }
    
    pcl::PointCloud<PointLT>::Ptr foreground_cloud(new pcl::PointCloud<PointLT>());
    for( int ll = 0 ; ll <= 2 ; ll++ )
    {
        std::cerr << "L" << ll <<" Inference!" << std::endl;
        bool reset_flag = ll == 0 ? true : false;
        if( ll >= 1 ) {
            triple_pooler.extractForeground(false);
        }
        triple_pooler.InputSemantics(binary_models[ll], ll, reset_flag, false);

    }

    foreground_cloud = triple_pooler.getSemanticLabels();
    if( viewer )
    {
        //viewer->addPointCloud(final_cloud, "labels");
        visualizeLabels(foreground_cloud, viewer, color_label);
        viewer->spin();
    }

    std::size_t numberOfObject = mesh_set.size();
    for(  size_t l = 0 ; l < foreground_cloud->size() ;l++ )
    {
        //red point cloud: background for object > 1?
        if( (foreground_cloud->at(l).label > 0 && numberOfObject == 0 )|| ( foreground_cloud->at(l).label > 1 && numberOfObject > 1 ))
            final_cloud.push_back(foreground_cloud->at(l));
    }

    std::vector<poseT> all_poses;
    /* POSE */
    if (compute_pose) {
        for (int i = 0; i < 1; ++i) { // was 99
            std::vector<poseT> all_poses1;

            pcl::PointCloud<myPointXYZ>::Ptr foreground(new pcl::PointCloud<myPointXYZ>());
            pcl::copyPointCloud(final_cloud, *foreground);

            if (bestPoseOnly)
                objrec->StandardBest(foreground, all_poses1);
            else
                objrec->StandardRecognize(foreground, all_poses1,minConfidence);

            pcl::PointCloud<myPointXYZ>::Ptr scene_xyz(new pcl::PointCloud<myPointXYZ>());
            pcl::copyPointCloud(*scene_f, *scene_xyz);

            all_poses =  RefinePoses(scene_xyz, mesh_set, all_poses1);
            std::cout << "# Poses found: " << all_poses.size() << std::endl;

            std::cout<<"done objrec ransac"<<std::endl;
            if( viewer )
            {
                std::cout<<"VISUALIZING"<<std::endl;
                viewer->removeAllPointClouds();
                viewer->addPointCloud(scene_f, "whole_scene");
                objrec->visualize_m(viewer, all_poses, model_name_map, color_label);
                viewer->spin();
                objrec->clearMesh(viewer, all_poses);
                viewer->removeAllPointClouds();
            }
        }

    }
    // normalize symmetric object Orientation
    normalizeAllModelOrientation (all_poses, objectDict);
    return all_poses;

}

bool getAndSaveTable (const sensor_msgs::PointCloud2 &pc, const ros::NodeHandle &nh)
{
    std::string tableTFname, tableTFparent;
    nh.param("tableTF", tableTFname,std::string("/tableTF"));
    
    listener->getParent(tableTFname,ros::Time(0),tableTFparent);
    if (listener->waitForTransform(tableTFparent,tableTFname,ros::Time::now(),ros::Duration(1.5)))
    {
        std::cerr << "Table TF with name: '" << tableTFname << "' found with parent frame: " << tableTFparent << std::endl;
        tf::StampedTransform transform;
        listener->lookupTransform(tableTFparent,tableTFname,ros::Time(0),transform);
        pcl::PointCloud<PointT>::Ptr full_cloud(new pcl::PointCloud<PointT>());

        fromROSMsg(inputCloud,*full_cloud);
        std::cerr << "PCL organized: " << full_cloud->isOrganized() << std::endl;
        volumeSegmentation(full_cloud,transform,0.5);
        tableConvexHull = getTableConvexHull(full_cloud);
        if (tableConvexHull->size() < 10) {
            std::cerr << "Retrying table segmentation...\n";
            return false;
        }
        
        bool saveTable;
        nh.param("updateTable",saveTable, true);
        if (saveTable){
            std::string saveTable_directory;
            nh.param("saveTable_directory",saveTable_directory,std::string("./data"));
            pcl::PCDWriter writer;
            writer.write<PointT> (saveTable_directory+"/table.pcd", *tableConvexHull, true);
            std::cerr << "Saved table point cloud in : " << saveTable_directory <<"/table.pcd"<<std::endl;
        }
        return true;
    }
    else {
        std::cerr << "Fail to get table TF with name: '" << tableTFname << "'" << std::endl;
        std::cerr << "Parent: " << tableTFparent << std::endl;
        return false;
    }
}

void updateCloudData (const sensor_msgs::PointCloud2 &pc)
{
    // The callback from main only update the cloud data
    inputCloud = pc;
}

bool serviceCallback (std_srvs::Empty::Request& request, std_srvs::Empty::Response& response)
{
    // Service call will run SPSegmenter
    pcl::PointCloud<PointT>::Ptr full_cloud(new pcl::PointCloud<PointT>());
    pcl::PointCloud<PointLT>::Ptr final_cloud(new pcl::PointCloud<PointLT>());

    fromROSMsg(inputCloud,*full_cloud); // convert to PCL format
    if (full_cloud->size() < 1){
        std::cerr << "No cloud available";
        return false;
    }
    
    segmentCloudAboveTable(full_cloud, tableConvexHull, aboveTable);
    
    if (full_cloud->size() < 1){
        std::cerr << "No cloud available after removing all object outside the table.\nPut some object above the table.\n";
        return false;
    }
    
    // get all poses from spSegmenterCallback
    std::vector<poseT> all_poses = spSegmenterCallback(full_cloud,*final_cloud);
    

    //publishing the segmented point cloud
    sensor_msgs::PointCloud2 output_msg;
    toROSMsg(*final_cloud,output_msg);
    output_msg.header.frame_id = inputCloud.header.frame_id;
    pc_pub.publish(output_msg);
    
    if (all_poses.size() < 1) {
        std::cerr << "Fail to segment objects on the table.\n";
        return false;
    }
    
    std::map<std::string, unsigned int> tmpIndex = objectTFIndex;
    for (poseT &p: all_poses)
    {
        segmentedObjectTF objectTmp(p,++tmpIndex[p.model_name]);
        segmentedObjectTFMap[objectTmp.TFnames] = objectTmp;
    }
    
    std::cerr << "Segmentation done.\n";
    
    hasTF = true;
    return true;
}

bool serviceCallbackGripper (sp_segmenter::segmentInGripper::Request & request, sp_segmenter::segmentInGripper::Response& response)
{
    std::cerr << "Segmenting object on gripper...\n";
    bool bestPoseOriginal = bestPoseOnly;
    bestPoseOnly = true; // only get best pose when segmenting object in gripper;
    
    pcl::PointCloud<PointT>::Ptr full_cloud(new pcl::PointCloud<PointT>());
    pcl::PointCloud<PointLT>::Ptr final_cloud(new pcl::PointCloud<PointLT>());
    std::string segmentFail("Object in gripper segmentation fails.");
    
    fromROSMsg(inputCloud,*full_cloud); // convert to PCL format
    if (full_cloud->size() < 1){
        std::cerr << "No cloud available";
        response.result = segmentFail;
        return false;
    }
    
    if (listener->waitForTransform(inputCloud.header.frame_id,gripperTF,ros::Time::now(),ros::Duration(1.5)))
    {
        tf::StampedTransform transform;
        listener->lookupTransform(inputCloud.header.frame_id,gripperTF,ros::Time(0),transform);
        // do a box segmentation around the gripper (50x50x50 cm)
        volumeSegmentation(full_cloud,transform,0.25,false);
        std::cerr << "Volume Segmentation done.\n";
    }
    else
    {
        std::cerr << "Fail to get transform between: "<< gripperTF << " and "<< inputCloud.header.frame_id << std::endl;
        response.result = segmentFail;
        return false;
    }
    
    if (full_cloud->size() < 1)
        std::cerr << "No cloud available around gripper. Make sure the object can be seen by the camera.\n";
    
    // get best poses from spSegmenterCallback
    std::vector<poseT> all_poses = spSegmenterCallback(full_cloud,*final_cloud);
    
    if (all_poses.size() < 1) {
        std::cerr << "Fail to segment the object around gripper.\n";
        response.result = segmentFail;
        return false;
    }
    
    //publishing the segmented point cloud
    sensor_msgs::PointCloud2 output_msg;
    toROSMsg(*final_cloud,output_msg);
    output_msg.header.frame_id = inputCloud.header.frame_id;
    pc_pub.publish(output_msg);
    
    for (poseT &p: all_poses)
    {
        segmentedObjectTF objectTmp(p, 0);
        objectTmp.TFnames = request.tfToUpdate;
        segmentedObjectTFMap[request.tfToUpdate] = objectTmp;
    }
    
    std::cerr << "Object In gripper segmentation done.\n";
    bestPoseOnly = bestPoseOriginal;
    hasTF = true;
    
    response.result = "Object In gripper segmentation done.\n";
    return true;
}

int main(int argc, char** argv)
{
    ros::init(argc,argv,"sp_segmenter_server");    
    ros::NodeHandle nh("~");
    ros::Rate r(10); //10Hz
    listener = new (tf::TransformListener);
    ros::ServiceServer spSegmenter = nh.advertiseService("SPSegmenter",serviceCallback);
    ros::ServiceServer segmentGripper = nh.advertiseService("segmentInGripper",serviceCallbackGripper);
    
    hasTF = false;
    static tf::TransformBroadcaster br;
    nh.param("GripperTF",gripperTF,std::string("endpoint_marker"));

// From SPSegmenter main Starts

    //std::string in_path("/home/chi/Downloads/");
    //std::string scene_name("UR5_1");

    //getting subscriber/publisher parameters
    nh.param("POINTS_IN", POINTS_IN,std::string("/camera/depth_registered/points"));
    nh.param("POINTS_OUT", POINTS_OUT,std::string("points_out"));
    nh.param("POSES_OUT", POSES_OUT,std::string("poses_out"));
    //get only best poses (1 pose output) or multiple poses
    nh.param("bestPoseOnly", bestPoseOnly, true);
    nh.param("minConfidence", minConfidence, 0.0);
    nh.param("aboveTable", aboveTable, 0.01);
    bool loadTable, haveTable = false;
    nh.param("loadTable",loadTable, true);

    if(loadTable)
    {
       std::string load_directory;
       nh.param("saveTable_directory",load_directory,std::string("./data"));
       pcl::PCDReader reader;
       if( reader.read (load_directory+"/table.pcd", *tableConvexHull) == 0){ 
            std::cerr << "Table load successfully\n";
           haveTable = true;
        }
       else {
            haveTable = false;
            std::cerr << "Fail to load table. Remove all objects, put the ar_tag marker in the center of the table and it will get anew table data\n";
        }
    }


    if (bestPoseOnly)
        std::cerr << "Node will only output the best detected poses \n";
    else
        std::cerr << "Node will output all detected poses \n";

    pc_sub = nh.subscribe(POINTS_IN,1,updateCloudData);
    pc_pub = nh.advertise<sensor_msgs::PointCloud2>(POINTS_OUT,1000);
    nh.param("pairWidth", pairWidth, 0.05);
    // pose_pub = nh.advertise<geometry_msgs::PoseArray>(POSES_OUT,1000);

    objrec = boost::shared_ptr<greedyObjRansac>(new greedyObjRansac(pairWidth, voxelSize));
    //pcl::console::parse_argument(argc, argv, "--p", in_path);
    //pcl::console::parse_argument(argc, argv, "--i", scene_name);
    
    //in_path = in_path + scene_name + "/";
    
    //std::string out_cloud_path("../../data_pool/IROS_demo/UR5_1/");
    //pcl::console::parse_argument(argc, argv, "--o", out_cloud_path);
    //boost::filesystem::create_directories(out_cloud_path);
    
    std::string svm_path,shot_path;
    //get path parameter for svm and shot
    nh.param("svm_path", svm_path,std::string("data/UR5_drill_svm/"));
    nh.param("shot_path", shot_path,std::string("data/UW_shot_dict/"));
//    std::string sift_path("UW_new_sift_dict/");
//    std::string fpfh_path("UW_fpfh_dict/");
/***************************************************************************************************************/
    
    float ratio = 0.1;
    
    pcl::console::parse_argument(argc, argv, "--rt", ratio);
    pcl::console::parse_argument(argc, argv, "--ss", down_ss);
    // pcl::console::parse_argument(argc, argv, "--zmax", zmax); not used anymore
    std::cerr << "Ratio: " << ratio << std::endl;
    std::cerr << "Downsample: " << down_ss << std::endl;
    
    std::string mesh_path;
    //get parameter for mesh path and cur_name
    nh.param("mesh_path", mesh_path,std::string("data/mesh/"));
    std::vector<std::string> cur_name = stringVectorArgsReader(nh, "cur_name", std::string("drill"));
    
    //get symmetry parameter of the objects
    objectDict = fillDictionary(nh, cur_name);

    for (int model_id = 0; model_id < cur_name.size(); model_id++)
    {
        // add all models. model_id starts in model_name start from 1.
        std::string temp_cur = cur_name.at(model_id);
        objrec->AddModel(mesh_path + temp_cur, temp_cur);
        model_name[model_id+1] = temp_cur;
        model_name_map[temp_cur] = model_id+1;
        ModelT mesh_buf = LoadMesh(mesh_path + temp_cur + ".obj", temp_cur);
        
        mesh_set.push_back(mesh_buf);
        objectTFIndex[temp_cur] = 0;
    }
    if( pcl::console::find_switch(argc, argv, "-v") == true )
        view_flag = true;
    if( pcl::console::find_switch(argc, argv, "-p") == true )
        compute_pose = true;

/***************************************************************************************************************/
    hie_producer = Hier_Pooler(radius);
    hie_producer.LoadDict_L0(shot_path, "200", "200");
    hie_producer.setRatio(ratio);
/***************************************************************************************************************/    
//    std::vector<cv::SiftFeatureDetector*> sift_det_vec;
//    for( float sigma = 0.7 ; sigma <= 1.61 ; sigma += 0.1 )
//    { 
//        cv::SiftFeatureDetector *sift_det = new cv::SiftFeatureDetector(
//          0, // nFeatures
//          4, // nOctaveLayers
//          -10000, // contrastThreshold 
//          100000, //edgeThreshold
//          sigma//sigma
//          );
//        sift_det_vec.push_back(sift_det); 
//    }
/***************************************************************************************************************/
//    std::vector< boost::shared_ptr<Pooler_L0> > sift_pooler_set(1+1);
//    for( size_t i = 1 ; i < sift_pooler_set.size() ; i++ )
//    {
//        boost::shared_ptr<Pooler_L0> cur_pooler(new Pooler_L0(-1));
//        sift_pooler_set[i] = cur_pooler;
//    }
//    sift_pooler_set[1]->LoadSeedsPool(sift_path+"dict_sift_L0_400.cvmat"); 
/***************************************************************************************************************/
//    std::vector< boost::shared_ptr<Pooler_L0> > fpfh_pooler_set(1+1);
//    for( size_t i = 1 ; i < fpfh_pooler_set.size() ; i++ )
//    {
//        boost::shared_ptr<Pooler_L0> cur_pooler(new Pooler_L0(-1));
//        fpfh_pooler_set[i] = cur_pooler;
//    }
//    fpfh_pooler_set[1]->LoadSeedsPool(fpfh_path+"dict_fpfh_L0_400.cvmat");
/***************************************************************************************************************/
    for( size_t i = 1 ; i < lab_pooler_set.size() ; i++ )
    {
        boost::shared_ptr<Pooler_L0> cur_pooler(new Pooler_L0);
        cur_pooler->setHSIPoolingParams(i);
        lab_pooler_set[i] = cur_pooler;
    }
/***************************************************************************************************************/
    for( int ll = 0 ; ll <= 2 ; ll++ )
    {
        std::stringstream ss;
        ss << ll;
        
        binary_models[ll] = load_model((svm_path+"binary_L"+ss.str()+"_f.model").c_str());
    }
    
/***************************************************************************************************************/
    
    if( view_flag )
    {
        viewer = pcl::visualization::PCLVisualizer::Ptr (new pcl::visualization::PCLVisualizer ("3D Viewer"));
        viewer->initCameraParameters();
        viewer->addCoordinateSystem(0.1);
        viewer->setCameraPosition(0, 0, 0.1, 0, 0, 1, 0, -1, 0);
        viewer->setSize(1280, 960);
    }
// From SPSegmenter ENDs

    while (ros::ok())
    {
        if (hasTF)
        {
            // broadcast all transform
            std::string parent = inputCloud.header.frame_id;
            // int index = 0;
            std::map<std::string, unsigned int> tmpIndex = objectTFIndex;
            for (std::map<std::string, segmentedObjectTF>::iterator it=segmentedObjectTFMap.begin(); it!=segmentedObjectTFMap.end(); ++it)
            {
                br.sendTransform(
                    it->second.generateStampedTransform(parent)
                    );
            }
        }
        ros::spinOnce();
        r.sleep();
        if (!haveTable) haveTable=getAndSaveTable(inputCloud, nh);        
    }

    for( int ll = 0 ; ll <= 2 ; ll++ )
        free_and_destroy_model(&binary_models[ll]);

    return 1;
}
