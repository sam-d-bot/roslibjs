
 
    #include <laser_scan_matcher/laser_scan_matcher_odom.h>
    #include <pcl_conversions/pcl_conversions.h>
 
    namespace scan_tools1
    {
 
    LaserScanMatcher::LaserScanMatcher(ros::NodeHandle nh, ros::NodeHandle nh_private):
                      nh_(nh),
                      nh_private_(nh_private)
    {
            ROS_INFO("Starting LaserScanMatcher");
 
            // **** init parameters
 
            initParams();
 
            // **** state variables
 
            initialized_   = false;
            received_imu_  = false;
            received_odom_ = false;
 
            w2b_.setIdentity();
 
            v_x_ = 0;
            v_y_ = 0;
            v_a_ = 0;
 
            vel_x_ = 0;
            vel_y_ = 0;
            vel_a_ = 0;
            last_vel_x_ = 0;
            last_vel_y_ = 0;
            last_vel_a_ = 0;
 
            input_.laser[0] = 0.0;
            input_.laser[1] = 0.0;
            input_.laser[2] = 0.0;
 
            // *** subscribers
 
            if (use_cloud_input_)
            {
                    cloud_subscriber_ = nh_.subscribe(
                                    cloud_topic_, 1, &LaserScanMatcher::cloudCallback, this);
 
            }
            else
            {
                    scan_subscriber_ = nh_.subscribe(
                                    scan_topic_, 1, &LaserScanMatcher::scanCallback, this);
            }
 
            if (use_imu_)
            {
                    imu_subscriber_ = nh_.subscribe(
                                    imu_topic_, 1, &LaserScanMatcher::imuCallback, this);
            }
 
            if (use_odom_)
            {
                    odom_subscriber_ = nh_.subscribe(
                                    odom_topic_, 1, &LaserScanMatcher::odomCallback, this);
            }
 
            // **** pose publisher
            if (publish_pose_)
            {
                    pose_publisher_  = nh_.advertise<geometry_msgs::Pose2D>(
                                    pose_topic_, 5);
            }
 
            if (publish_vel_)
            {
                    vel_publisher_ = nh_.advertise<nav_msgs::Odometry>(odom_topic_, 100);
            }
    }
 
 
    LaserScanMatcher::~LaserScanMatcher()
    {
 
    }
 
    void LaserScanMatcher::initParams()
    {
            if (!nh_private_.getParam ("base_frame", base_frame_))
                    base_frame_ = "base_link";
            if (!nh_private_.getParam ("fixed_frame", fixed_frame_))
                    fixed_frame_ = "world";
 
            // **** input type - laser scan, or point clouds?
            // if false, will subscrive to LaserScan msgs on /scan.
            // if true, will subscrive to PointCloud2 msgs on /cloud
 
            if (!nh_private_.getParam ("use_cloud_input", use_cloud_input_))
                    use_cloud_input_= false;
 
            if (use_cloud_input_)
            {
                    if (!nh_private_.getParam ("cloud_range_min", cloud_range_min_))
                            cloud_range_min_ = 0.1;
                    if (!nh_private_.getParam ("cloud_range_max", cloud_range_max_))
                            cloud_range_max_ = 50.0;
 
                    input_.min_reading = cloud_range_min_;
                    input_.max_reading = cloud_range_max_;
            }
 
            // **** What predictions are available to speed up the ICP?
            // 1) imu - [theta] from imu yaw angle - /odom topic
            // 2) odom - [x, y, theta] from wheel odometry - /imu topic
            // 3) alpha_beta - [x, y, theta] from simple tracking filter - no topic req.
            // If more than one is enabled, priority is imu > odom > alpha_beta
 
            if (!nh_private_.getParam ("use_imu", use_imu_))
                    use_imu_ = false;
            if (!nh_private_.getParam ("use_odom", use_odom_))
                    use_odom_ = false;
            if (!nh_private_.getParam ("use_alpha_beta", use_alpha_beta_))
                    use_alpha_beta_ = true;
            if (!nh_private_.getParam ("publish_vel", publish_vel_))
                    publish_vel_ = true;
 
            // **** How to publish the output?
            // tf (fixed_frame->base_frame),
            // pose message (pose of base frame in the fixed frame)
 
            if (!nh_private_.getParam ("publish_tf", publish_tf_))
                    publish_tf_ = true;
            if (!nh_private_.getParam ("publish_pose", publish_pose_))
                    publish_pose_ = true;
 
            if (!nh_private_.getParam ("alpha", alpha_))
                    alpha_ = 1.0;
            if (!nh_private_.getParam ("beta", beta_))
                    beta_ = 0.8;
 
            // **** CSM parameters - comments copied from algos.h (by Andrea Censi)
 
            // Maximum angular displacement between scans
            if (!nh_private_.getParam ("max_angular_correction_deg", input_.max_angular_correction_deg))
                    input_.max_angular_correction_deg = 45.0;
 
            // Maximum translation between scans (m)
            if (!nh_private_.getParam ("max_linear_correction", input_.max_linear_correction))
                    input_.max_linear_correction = 0.50;
 
            // Maximum ICP cycle iterations
            if (!nh_private_.getParam ("max_iterations", input_.max_iterations))
                    input_.max_iterations = 10;
 
            // A threshold for stopping (m)
            if (!nh_private_.getParam ("epsilon_xy", input_.epsilon_xy))
                    input_.epsilon_xy = 0.000001;
 
            // A threshold for stopping (rad)
            if (!nh_private_.getParam ("epsilon_theta", input_.epsilon_theta))
                    input_.epsilon_theta = 0.000001;
 
            // Maximum distance for a correspondence to be valid
            if (!nh_private_.getParam ("max_correspondence_dist", input_.max_correspondence_dist))
                    input_.max_correspondence_dist = 0.3;
 
            // Noise in the scan (m)
            if (!nh_private_.getParam ("sigma", input_.sigma))
                    input_.sigma = 0.010;
 
            // Use smart tricks for finding correspondences.
            if (!nh_private_.getParam ("use_corr_tricks", input_.use_corr_tricks))
                    input_.use_corr_tricks = 1;
 
            // Restart: Restart if error is over threshold
            if (!nh_private_.getParam ("restart", input_.restart))
                    input_.restart = 0;
 
            // Restart: Threshold for restarting
            if (!nh_private_.getParam ("restart_threshold_mean_error", input_.restart_threshold_mean_error))
                    input_.restart_threshold_mean_error = 0.01;
 
            // Restart: displacement for restarting. (m)
            if (!nh_private_.getParam ("restart_dt", input_.restart_dt))
                    input_.restart_dt = 1.0;
 
            // Restart: displacement for restarting. (rad)
            if (!nh_private_.getParam ("restart_dtheta", input_.restart_dtheta))
                    input_.restart_dtheta = 0.1;
 
            // Max distance for staying in the same clustering
            if (!nh_private_.getParam ("clustering_threshold", input_.clustering_threshold))
                    input_.clustering_threshold = 0.25;
 
            // Number of neighbour rays used to estimate the orientation
            if (!nh_private_.getParam ("orientation_neighbourhood", input_.orientation_neighbourhood))
                    input_.orientation_neighbourhood = 20;
 
            // If 0, it's vanilla ICP
            if (!nh_private_.getParam ("use_point_to_line_distance", input_.use_point_to_line_distance))
                    input_.use_point_to_line_distance = 1;
 
            // Discard correspondences based on the angles
            if (!nh_private_.getParam ("do_alpha_test", input_.do_alpha_test))
                    input_.do_alpha_test = 0;
 
            // Discard correspondences based on the angles - threshold angle, in degrees
            if (!nh_private_.getParam ("do_alpha_test_thresholdDeg", input_.do_alpha_test_thresholdDeg))
                    input_.do_alpha_test_thresholdDeg = 20.0;
 
            // Percentage of correspondences to consider: if 0.9,
            // always discard the top 10% of correspondences with more error
            if (!nh_private_.getParam ("outliers_maxPerc", input_.outliers_maxPerc))
                    input_.outliers_maxPerc = 0.90;
 
            // Parameters describing a simple adaptive algorithm for discarding.
            //  1) Order the errors.
            //      2) Choose the percentile according to outliers_adaptive_order.
            //         (if it is 0.7, get the 70% percentile)
            //      3) Define an adaptive threshold multiplying outliers_adaptive_mult
            //         with the value of the error at the chosen percentile.
            //      4) Discard correspondences over the threshold.
            //      This is useful to be conservative; yet remove the biggest errors.
            if (!nh_private_.getParam ("outliers_adaptive_order", input_.outliers_adaptive_order))
                    input_.outliers_adaptive_order = 0.7;
 
            if (!nh_private_.getParam ("outliers_adaptive_mult", input_.outliers_adaptive_mult))
                    input_.outliers_adaptive_mult = 2.0;
 
            //If you already have a guess of the solution, you can compute the polar angle
            //      of the points of one scan in the new position. If the polar angle is not a monotone
            //      function of the readings index, it means that the surface is not visible in the
            //      next position. If it is not visible, then we don't use it for matching.
            if (!nh_private_.getParam ("do_visibility_test", input_.do_visibility_test))
                    input_.do_visibility_test = 0;
 
            // no two points in laser_sens can have the same corr.
            if (!nh_private_.getParam ("outliers_remove_doubles", input_.outliers_remove_doubles))
                    input_.outliers_remove_doubles = 1;
 
            // If 1, computes the covariance of ICP using the method http://purl.org/censi/2006/icpcov
            if (!nh_private_.getParam ("do_compute_covariance", input_.do_compute_covariance))
                    input_.do_compute_covariance = 0;
 
            // Checks that find_correspondences_tricks gives the right answer
            if (!nh_private_.getParam ("debug_verify_tricks", input_.debug_verify_tricks))
                    input_.debug_verify_tricks = 0;
 
            // If 1, the field 'true_alpha' (or 'alpha') in the first scan is used to compute the
            // incidence beta, and the factor (1/cos^2(beta)) used to weight the correspondence.");
            if (!nh_private_.getParam ("use_ml_weights", input_.use_ml_weights))
                    input_.use_ml_weights = 0;
 
            // If 1, the field 'readings_sigma' in the second scan is used to weight the
            // correspondence by 1/sigma^2
            if (!nh_private_.getParam ("use_sigma_weights", input_.use_sigma_weights))
                    input_.use_sigma_weights = 0;
    }
 
    void LaserScanMatcher::imuCallback (const sensor_msgs::ImuPtr& imu_msg)
    {
            boost::mutex::scoped_lock(mutex_);
            latest_imu_yaw_ = getYawFromQuaternion(imu_msg->orientation);
            if (!received_imu_)
            {
                    last_imu_yaw_ = getYawFromQuaternion(imu_msg->orientation);
                    received_imu_ = true;
            }
    }
 
    void LaserScanMatcher::odomCallback (const nav_msgs::Odometry::ConstPtr& odom_msg)
    {
            boost::mutex::scoped_lock(mutex_);
            latest_odom_ = *odom_msg;
            if (!received_odom_)
            {
                    last_odom_ = *odom_msg;
                    received_odom_ = true;
            }
    }
 
    void LaserScanMatcher::cloudCallback (const PointCloudT::ConstPtr& cloud)
    {
            // **** if first scan, cache the tf from base to the scanner
            std_msgs::Header cloud_header = pcl_conversions::fromPCL(cloud->header);	
 
            if (!initialized_)
            {
                    // cache the static tf from base to laser
                    if (!getBaseToLaserTf(cloud->header.frame_id))
                    {
                            ROS_WARN("ScanMatcher: Skipping scan");
                            return;
                    }
 
                    PointCloudToLDP(cloud, prev_ldp_scan_);
                    last_icp_time_ = cloud_header.stamp;
                    last_imu_yaw_ = latest_imu_yaw_;
                    last_odom_ = latest_odom_;
                    initialized_ = true;
            }
 
            LDP curr_ldp_scan;
            PointCloudToLDP(cloud, curr_ldp_scan);
            processScan(curr_ldp_scan, cloud_header.stamp);
    }
 
    void LaserScanMatcher::scanCallback (const sensor_msgs::LaserScan::ConstPtr& scan_msg)
    {
            // **** if first scan, cache the tf from base to the scanner
 
            if (!initialized_)
            {
                    createCache(scan_msg);    // caches the sin and cos of all angles
 
                    // cache the static tf from base to laser
                    if (!getBaseToLaserTf(scan_msg->header.frame_id))
                    {
                            ROS_WARN("ScanMatcher: Skipping scan");
                            return;
                    }
 
                    laserScanToLDP(scan_msg, prev_ldp_scan_);
                    last_icp_time_ = scan_msg->header.stamp;
                    last_imu_yaw_ = latest_imu_yaw_;
                    last_odom_ = latest_odom_;
                    initialized_ = true;
            }
 
            LDP curr_ldp_scan;
            laserScanToLDP(scan_msg, curr_ldp_scan);
            processScan(curr_ldp_scan, scan_msg->header.stamp);
    }
 
    void LaserScanMatcher::processScan(LDP& curr_ldp_scan, const ros::Time& time)
    {
            struct timeval start, end;    // used for timing
            gettimeofday(&start, NULL);
 
            // CSM is used in the following way:
            // The scans are always in the laser frame
            // The reference scan (prevLDPcan_) has a pose of 0
            // The new scan (currLDPScan) has a pose equal to the movement
            // of the laser in the laser frame since the last scan
            // The computed correction is then propagated using the tf machinery
 
            prev_ldp_scan_->odometry[0] = 0;
            prev_ldp_scan_->odometry[1] = 0;
            prev_ldp_scan_->odometry[2] = 0;
 
            prev_ldp_scan_->estimate[0] = 0;
            prev_ldp_scan_->estimate[1] = 0;
            prev_ldp_scan_->estimate[2] = 0;
 
            prev_ldp_scan_->true_pose[0] = 0;
            prev_ldp_scan_->true_pose[1] = 0;
            prev_ldp_scan_->true_pose[2] = 0;
 
            input_.laser_ref  = prev_ldp_scan_;
            input_.laser_sens = curr_ldp_scan;
 
            // **** estimated change since last scan
 
            ros::Time new_icp_time = ros::Time::now();
            ros::Duration dur = new_icp_time - last_icp_time_;
            double dt = dur.toSec();
 
            double pr_ch_x, pr_ch_y, pr_ch_a;
            getPrediction(pr_ch_x, pr_ch_y, pr_ch_a, dt);
 
            // the predicted change of the laser's position, in the base frame
 
            tf::Transform pr_ch;
            createTfFromXYTheta(pr_ch_x, pr_ch_y, pr_ch_a, pr_ch);
 
            // the predicted change of the laser's position, in the laser frame
 
            tf::Transform pr_ch_l;
            pr_ch_l = laser_to_base_ * pr_ch * base_to_laser_;
 
            input_.first_guess[0] = pr_ch_l.getOrigin().getX();
            input_.first_guess[1] = pr_ch_l.getOrigin().getY();
            input_.first_guess[2] = getYawFromQuaternion(pr_ch_l.getRotation());
 
            /*
      printf("%f, %f, %f\n", input_.first_guess[0],  
                             input_.first_guess[1],
                             input_.first_guess[2]);
             */
            // *** scan match - using point to line icp from CSM
 
            sm_icp(&input_, &output_);
 
            if (output_.valid)
            {
                    // the correction of the laser's position, in the laser frame
 
                    //printf("%f, %f, %f\n", output_.x[0], output_.x[1], output_.x[2]);
 
                    tf::Transform corr_ch_l;
                    createTfFromXYTheta(output_.x[0], output_.x[1], output_.x[2], corr_ch_l);
 
                    // the correction of the base's position, in the world frame
 
                    tf::Transform corr_ch = base_to_laser_ * corr_ch_l * laser_to_base_;
 
                    if(use_alpha_beta_ || publish_vel_)
                    {
                            tf::Transform w2b_new = w2b_ * corr_ch;
 
                            double dx = w2b_new.getOrigin().getX() - w2b_.getOrigin().getX();
                            double dy = w2b_new.getOrigin().getY() - w2b_.getOrigin().getY();
                            double da = getYawFromQuaternion(w2b_new.getRotation()) -
                                            getYawFromQuaternion(w2b_.getRotation());
 
                            double r_x = dx - pr_ch_x;
                            double r_y = dy - pr_ch_y;
                            double r_a = da - pr_ch_a;
 
                            double x = w2b_.getOrigin().getX();
                            double y = w2b_.getOrigin().getY();
                            double a = getYawFromQuaternion(w2b_.getRotation());
 
                            double x_new  = (x + pr_ch_x) + alpha_ * r_x;
                            double y_new  = (y + pr_ch_y) + alpha_ * r_y;
                            double a_new  = (a + pr_ch_a) + alpha_ * r_a;
 
                            createTfFromXYTheta(x_new, y_new, a_new, w2b_);
 
                            if (dt != 0.0)
                            {
                                    v_x_ = v_x_ + (beta_ / dt) * r_x;
                                    v_y_ = v_y_ + (beta_ / dt) * r_y;
                                    v_a_ = v_a_ + (beta_ / dt) * r_a;
 
                                    //TODO
                                    //Low-pass filter, 4.9Hz cutoff first order Butterworth
                                    //Warning: non-linear phase, be careful in control applications
                                    //http://www-users.cs.york.ac.uk/~fisher/mkfilter/trad.html
                                    double vx_raw = dx / dt / 1.031426266;
                                    double vy_raw = dy / dt / 1.031426266;
                                    double va_raw = da / dt / 1.031426266;
 
                                    vel_x_ = vx_raw + last_vel_x_ - 0.9390625058 * vel_x_;
                                    vel_y_ = vy_raw + last_vel_y_ - 0.9390625058 * vel_y_;
                                    vel_a_ = va_raw + last_vel_a_ - 0.9390625058 * vel_a_;
 
                                    last_vel_x_ = vx_raw;
                                    last_vel_y_ = vy_raw;
                                    last_vel_a_ = va_raw;
                            }
                    }
                    else
                    {
                            w2b_ = w2b_ * corr_ch;
                    }
 
                    // **** publish
 
                    if (publish_pose_)
                    {
                            geometry_msgs::Pose2D::Ptr pose_msg;
                            pose_msg = boost::make_shared<geometry_msgs::Pose2D>();
                            pose_msg->x = w2b_.getOrigin().getX();
                            pose_msg->y = w2b_.getOrigin().getY();
                            pose_msg->theta = getYawFromQuaternion(w2b_.getRotation());
                            pose_publisher_.publish(pose_msg);
                    }
                    if (publish_tf_)
                    {
                            tf::StampedTransform transform_msg (w2b_, time, fixed_frame_, base_frame_);
                            tf_broadcaster_.sendTransform (transform_msg);
                    }
                    if (publish_vel_)
                    {
                            odom.header.stamp = time;
                            odom.header.frame_id = fixed_frame_;
 
                            odom.pose.pose.orientation.x = 0;
                            odom.pose.pose.orientation.y = 0;
                            odom.pose.pose.orientation.z = 0;
                            odom.pose.pose.orientation.w = 1;
 
                            odom.child_frame_id = base_frame_;     
                            odom.twist.twist.linear.x = vel_x_;
                            odom.twist.twist.linear.y = vel_y_;
                            odom.twist.twist.angular.z = vel_a_;
 
                            vel_publisher_.publish(odom);
                    }
            }
            else
            {
                    ROS_WARN("Error in scan matching");
 
                    v_x_ = 0.0;
                    v_y_ = 0.0;
                    v_a_ = 0.0;
            }
 
            // **** swap old and new
 
            ld_free(prev_ldp_scan_);
            prev_ldp_scan_ = curr_ldp_scan;
            last_icp_time_ = new_icp_time;
 
            // **** statistics
 
            gettimeofday(&end, NULL);
            double dur_total = ((end.tv_sec   * 1000000 + end.tv_usec  ) -
                            (start.tv_sec * 1000000 + start.tv_usec)) / 1000.0;
            ROS_DEBUG("scan matcher total duration: %.1f ms", dur_total);
    }
 
    void LaserScanMatcher::PointCloudToLDP(const PointCloudT::ConstPtr& cloud,
                    LDP& ldp)
    {
            unsigned int n = cloud->width * cloud->height ;
            ldp = ld_alloc_new(n);
 
            for (unsigned int i = 0; i < n; i++)
            {
                    // calculate position in laser frame
 
                    if (is_nan(cloud->points[i].x) || is_nan(cloud->points[i].y))
                    {
                            ROS_WARN("Laser Scan Matcher: Cloud input contains NaN values. \
                   Please use a filtered cloud input.");
                    }
                    else
                    {
                            double r = sqrt(cloud->points[i].x * cloud->points[i].x +
                                            cloud->points[i].y * cloud->points[i].y);
 
                            if (r > cloud_range_min_ && r < cloud_range_max_)
                            {
                                    ldp->valid[i] = 1;
                                    ldp->readings[i] = r;
                            }
                            else
                            {
                                    ldp->valid[i] = 0;
                                    ldp->readings[i] = -1;  // for invalid range
                            }
                    }
 
                    ldp->theta[i] = atan2(cloud->points[i].y, cloud->points[i].x);
                    ldp->cluster[i]  = -1;
            }
 
            ldp->min_theta = ldp->theta[0];
            ldp->max_theta = ldp->theta[n-1];
 
            ldp->odometry[0] = 0.0;
            ldp->odometry[1] = 0.0;
            ldp->odometry[2] = 0.0;
 
            ldp->true_pose[0] = 0.0;
            ldp->true_pose[1] = 0.0;
            ldp->true_pose[2] = 0.0;
    }
 
    void LaserScanMatcher::laserScanToLDP(const sensor_msgs::LaserScan::ConstPtr& scan_msg,
                    LDP& ldp)
    {
            unsigned int n = scan_msg->ranges.size();
            ldp = ld_alloc_new(n);
 
            for (unsigned int i = 0; i < n; i++)
            {
                    // calculate position in laser frame
 
                    double r = scan_msg->ranges[i];
 
                    if (r > scan_msg->range_min && r < scan_msg->range_max)
                    {
                            // fill in laser scan data
 
                            ldp->valid[i] = 1;
                            ldp->readings[i] = r;
                    }
                    else
                    {
                            ldp->valid[i] = 0;
                            ldp->readings[i] = -1;  // for invalid range
                    }
 
                    ldp->theta[i]    = scan_msg->angle_min + i * scan_msg->angle_increment;
 
                    ldp->cluster[i]  = -1;
            }
 
            ldp->min_theta = ldp->theta[0];
            ldp->max_theta = ldp->theta[n-1];
 
            ldp->odometry[0] = 0.0;
            ldp->odometry[1] = 0.0;
            ldp->odometry[2] = 0.0;
 
            ldp->true_pose[0] = 0.0;
            ldp->true_pose[1] = 0.0;
            ldp->true_pose[2] = 0.0;
    }
 
    void LaserScanMatcher::createCache (const sensor_msgs::LaserScan::ConstPtr& scan_msg)
    {
            a_cos_.clear();
            a_sin_.clear();
 
            for (unsigned int i = 0; i < scan_msg->ranges.size(); ++i)
            {
                    double angle = scan_msg->angle_min + i * scan_msg->angle_increment;
                    a_cos_.push_back(cos(angle));
                    a_sin_.push_back(sin(angle));
            }
 
            input_.min_reading = scan_msg->range_min;
            input_.max_reading = scan_msg->range_max;
    }
 
    bool LaserScanMatcher::getBaseToLaserTf (const std::string& frame_id)
    {
            ros::Time t = ros::Time::now();
 
            tf::StampedTransform base_to_laser_tf;
            try
            {
                    tf_listener_.waitForTransform(
                                    base_frame_, frame_id, t, ros::Duration(1.0));
                    tf_listener_.lookupTransform (
                                    base_frame_, frame_id, t, base_to_laser_tf);
            }
            catch (tf::TransformException ex)
            {
                    ROS_WARN("ScanMatcher: Could not get initial laser transform(%s)", ex.what());
                    return false;
            }
            base_to_laser_ = base_to_laser_tf;
            laser_to_base_ = base_to_laser_.inverse();
 
            return true;
    }
 
    // returns the predicted change in pose (in fixed frame)
    // since the last time we did icp
    void LaserScanMatcher::getPrediction(double& pr_ch_x, double& pr_ch_y,
                    double& pr_ch_a, double dt)
    {
            boost::mutex::scoped_lock(mutex_);
 
            // **** base case - no input available, use zero-motion model
            pr_ch_x = 0.0;
            pr_ch_y = 0.0;
            pr_ch_a = 0.0;
 
            // **** use alpha-beta tracking (const. vel. model)
            if (use_alpha_beta_)
            {
                    // estmate change in fixed frame, using fixed velocity
                    pr_ch_x = v_x_ * dt;     // in fixed frame
                    pr_ch_y = v_y_ * dt;
                    pr_ch_a = v_a_ * dt;
            }
 
            // **** use wheel odometry
            if (use_odom_ && received_odom_)
            {
                    pr_ch_x = latest_odom_.pose.pose.position.x -
                                    last_odom_.pose.pose.position.x;
 
                    pr_ch_y = latest_odom_.pose.pose.position.y -
                                    last_odom_.pose.pose.position.y;
 
                    pr_ch_a = getYawFromQuaternion(latest_odom_.pose.pose.orientation) -
                                    getYawFromQuaternion(last_odom_.pose.pose.orientation);
 
                    last_odom_ = latest_odom_;
            }
 
            // **** use imu
            if (use_imu_ && received_imu_)
            {
                    pr_ch_a = latest_imu_yaw_ - last_imu_yaw_;
                    last_imu_yaw_ = latest_imu_yaw_;
            }
    }
 
    double LaserScanMatcher::getYawFromQuaternion(
                    const tf::Quaternion& quaternion)
    {
            double temp, yaw;
            tf::Matrix3x3 m(quaternion);
            m.getRPY(temp, temp, yaw);
            return yaw;
    }
 
    double LaserScanMatcher::getYawFromQuaternion(
                    const geometry_msgs::Quaternion& quaternion)
    {
            tf::Quaternion q;
            tf::quaternionMsgToTF(quaternion, q);
            return getYawFromQuaternion(q);
    }
 
    void LaserScanMatcher::createTfFromXYTheta(
                    double x, double y, double theta, tf::Transform& t)
    {
            t.setOrigin(tf::Vector3(x, y, 0.0));
            tf::Quaternion q;
            q.setRPY(0.0, 0.0, theta);
            t.setRotation(q);
    }
 
    } //namespace