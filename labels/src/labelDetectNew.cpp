//ros includes and ros messages
#include <ros/ros.h> 
#include <std_msgs/String.h>
#include <std_msgs/Int32.h>
#include <geometry_msgs/Vector3.h>
#include <geometry_msgs/Vector3Stamped.h>
#include <geometry_msgs/Twist.h>
#include "ardrone_autonomy/Navdata.h"
#include <sensor_msgs/image_encodings.h>
#include <image_transport/image_transport.h>
#include <string>

//opencv includes
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <cv_bridge/cv_bridge.h>
#include <opencv2/core/core.hpp>
#include <opencv2/features2d/features2d.hpp>
#include <opencv2/nonfree/nonfree.hpp>
#include <opencv2/calib3d/calib3d.hpp>

//C++ includes
#include <math.h>
#include <mutex>
#include <algorithm>
#include <fstream>

int prevAngle=-1; 
int haveRead= 0; //Reading from file mode started or not
int prevSum=-1;

#define IDLE 0
#define READING 1
#define HOMING 2
#define READFROMFILE 3
#define NUMLABELS 16 
#define erosion_size 1 //change for more erosion

#define PI 3.14159

float angles[NUMLABELS]; //RWB,RGB,GWB,GRB,BRG,WRB,BGR,WBR,WRG,GBR,BWR,WGR,WGB,RBG,WBG,RWG
using namespace cv;
using namespace std;
float currAngle;
mutex currAngleLock, angleArrayLock;
int returned[5]={-1,-1,-1,-1,-1};
int currStatus=IDLE; //initialize state with IDLE


geometry_msgs::Twist thetasMessage; //message that contains theta angles used in planner wrt N of earth.
std_msgs::Int32 flyingMessage; //message for displaying whether 3 labels are detected

float fmodAng(float a) //not used
{
    return (a<0)*360.0-(a>360)*360.0+a;
}

float dist(Point2f a1,Point2f a2) //dist between 2 points
{
  return sqrt(pow(a2.x-a1.x,2)+pow(a2.y-a1.y,2));
}

int isnonzero(float* array, int arraysize) //function to check if all elements are non zero, i.e. reading is complete
{
    for(int i=0;i<arraysize;i++)
    {
        if (fabs(array[i])<0.001)
        {
            return 0;
        }
    }
    return 1;
}

int detectPoster(std::vector<KeyPoint> KR, std::vector<KeyPoint> KG, std::vector<KeyPoint> KB) //function to detect labels given 3 keypoints
{
    for(int i=0;i<KR.size();i++)
    {
        for(int j=0;j<KB.size();j++)
        {
            
            for(int k=0;k<KG.size();k++)
            {
                if(dist(KG[k].pt,KB[j].pt)<2*KG[k].size && dist(KG[k].pt,KR[i].pt)<2*KG[k].size)
                {
                    if(KB[j].pt.y < KR[i].pt.y)
                    {
                        //cout<<"BGR"<<endl;
                        return 6;
                    }
                    else if(KB[j].pt.y > KR[i].pt.y)
                    {
                        //cout<<"RGB"<<endl;
                        return 1;
                    }
                }
                else if(dist(KR[i].pt,KB[j].pt)<2*KR[i].size && dist(KG[k].pt,KR[i].pt)<2*KR[i].size)
                {
                    if(KB[j].pt.y < KG[k].pt.y)
                    {
                        //cout<<"BRG"<<endl;
                        return 4;
                    }
                    else if(KB[j].pt.y > KG[k].pt.y)
                    {
                        //cout<<"GRB"<<endl;
                        return 3;
                    }
                }
                else if (dist(KR[i].pt,KB[j].pt)<2*KB[j].size && dist(KG[k].pt,KB[j].pt)<2*KB[j].size)
                {
                    if(KR[i].pt.y > KG[k].pt.y)
                    {
                       // cout<<"GBR"<<endl;
                        return 9;
                    }
                    else if(KR[i].pt.y < KG[k].pt.y)
                    {
                       // cout<<"RBG"<<endl;
                        return 13;
                    }
                }
            }
            //cout<<dist(KR[i].pt,KB[j].pt)<<" "<<KR[i].size<<endl;
            if(dist(KR[i].pt,KB[j].pt)<2*KR[i].size)
            {
                if(KB[j].pt.y < KR[i].pt.y)
                {
                    //cout<<"WBR"<<endl;
                    return 7;
                }
                else
                {
                    //cout<<"WRB"<<endl;
                    return 5;
                }
            }
            else if (dist(KR[i].pt,KB[j].pt)<4*KR[i].size)
            { 
                if(KB[j].pt.y < KR[i].pt.y)
                {
                    //cout<<"BWR"<<endl;
                    return 10;
                }
                else
                {
                    //cout<<"RWB"<<endl;
                    return 0;
                }
            }
        }
        for(int k=0;k<KG.size();k++)
        {
            if(dist(KG[k].pt,KR[i].pt)<2*KG[k].size)
            {
                if(KR[i].pt.y < KG[k].pt.y)
                {
                    //cout<<"WRG"<<endl;
                    return 8;
                }
                else
                {
                    //cout<<"WGR"<<endl;
                    return 11;
                }
            }
            else if (dist(KG[k].pt,KR[i].pt)<4*KG[k].size)
            { 
                if(KR[i].pt.y < KG[i].pt.y)
                {
                    //cout<<"RWG"<<endl;
                    return 15;
                }
            }        
        }
    }
    for(int j=0;j<KB.size();j++)
    {
        for(int k=0;k<KG.size();k++)
        {
            if(dist(KG[k].pt,KB[j].pt)<2*KG[k].size)
            {
                if(KB[j].pt.y < KG[k].pt.y)
                {
                    //cout<<"WBG"<<endl;
                    return 14;
                }
                else
                {
                    //cout<<"WGB"<<endl;
                    return 12;
                }
            }
            else if (dist(KG[k].pt,KB[j].pt)<4*KG[k].size)
            { 
                if(KB[j].pt.y > KG[k].pt.y)
                {
                    //cout<<"GWB"<<endl;
                    return 2;
                }
            }               
        }

    }
return -1;
}

bool myfunction (int i,int j) //modified function to return opposite if 0, 14 & 0,15 etc are encountered.
{ 
    if(i==0 && j==NUMLABELS-1)
        return (i>j);
    else if(i==NUMLABELS-1 && j==0)
        return (i>j);
    else if(i==NUMLABELS-2 && j==0)
        return (i>j);
    else if(i==0 && j==NUMLABELS-2)
        return (i>j);
    else if(i==1 && j==NUMLABELS-1)
        return (i>j);
    else if(i==NUMLABELS-1 && j==1)
        return (i>j);
    else
        return (i<j); 
}


bool myfunction2 (int i,int j) 
{
    return (i<j); 
}

void statusCallBack(const std_msgs::Int32::ConstPtr& msg) 
{
    currStatus=msg->data; //currStatus from teleop code
}

void magCallBack(const geometry_msgs::Vector3Stamped::ConstPtr& msg) //simply stores the current magnetometer readings and converts it into angles.
{
  currAngleLock.lock();
  geometry_msgs::Vector3 magData=msg->vector;
  float x_val=magData.x;
  float y_val=magData.y;
  currAngle=atan2(y_val,x_val)*180.0/PI;
  currAngleLock.unlock();
}

void imageCallback(const sensor_msgs::ImageConstPtr &msg) //image processing callback function.
{ 
        if(currStatus==IDLE) //do nothing
        {
            return;
        }

        if(currStatus==READFROMFILE) //read all angles from the txt file
        {
            angleArrayLock.lock();
            char data[65];
            ifstream infile; 
            infile.open("/home/thesidjway/ardrone_ws/src/angles.txt");
            infile >> data; 
            for (int a = 0; a < 16; a++)
            {   
                int hund=(int)data[4*a]-48;
                int tens=(int)data[4*a+1]-48;
                int ones=(int)data[4*a+2]-48;
                int number=hund*100+tens*10+ones;
                cout<<number<<endl;
                angles[a]=number-360; //stored all numbers added with 360, so subtract 360.
            }
            infile.close();
            cout<<"Done Reading!"<<endl;
            angleArrayLock.unlock();
            haveRead=1;
            return;
        }

        Rect myROI(260,0,120,360);
        Rect myROI2(0,0,640,240);
        Mat srcred1,srcred2,srcred,srcdark,srcblue,srcgreen,srcwhite,dilatedblue,initgreen;
        Mat detectionImgFull,detectionImg;
        Mat detectionImgHSV,detectionImgGray;

        cv_bridge::CvImagePtr cv_ptr; //library to convert ros image messages to OpenCV types

        try
        {
          cv_ptr = cv_bridge::toCvCopy(msg, sensor_msgs::image_encodings::BGR8); 
        }
        catch (cv_bridge::Exception& e)
        {
          ROS_ERROR("cv_bridge exception: %s", e.what());
          return;
        }

        detectionImgFull=cv_ptr->image;
        if(currStatus==READING)
        {
            detectionImg=detectionImgFull(myROI); 
        }
        else if(currStatus==HOMING)
        {
            detectionImg=detectionImgFull(myROI2);
        }



        
        Mat element = getStructuringElement( MORPH_CROSS,
                                       Size( 2*erosion_size + 1, 2*erosion_size+1 ),
                                       Point( erosion_size, erosion_size )); //3x3 element for erosion/dilation



        
        cvtColor(detectionImg,detectionImgHSV,CV_BGR2HSV);
        cvtColor(detectionImg,detectionImgGray,CV_BGR2GRAY);


        inRange(detectionImgHSV,Scalar(0,50,40),Scalar(4,255,255),srcred1);
        inRange(detectionImgHSV,Scalar(170,50,40),Scalar(180,255,255),srcred2);
        bitwise_or(srcred1,srcred2,srcred);

        inRange(detectionImgHSV,Scalar(55,40,20),Scalar(88,255,255),initgreen);
        inRange(detectionImgHSV,Scalar(108,40,20),Scalar(132,255,255),dilatedblue);

        inRange(detectionImgGray,0,99,srcdark);
        inRange(detectionImgGray,100,255,srcwhite);

        bitwise_and(srcdark,dilatedblue,dilatedblue);
        bitwise_and(srcdark,initgreen,initgreen);
        bitwise_and(srcdark,srcred,srcred);

        erode(dilatedblue,srcblue,element);
        dilate(srcblue,srcblue,element);

        dilate(initgreen,srcgreen,element);


        //imshow("GREEN",srcblue);

        SimpleBlobDetector::Params params;

        //check blob detector params link from readme file to know about this. 
        params.filterByColor = true; 
        params.blobColor = 255;
        params.minThreshold = 200;
        params.filterByArea = true;
        params.minArea = 30;
        params.filterByCircularity = false;
        params.filterByConvexity = true;
        params.minConvexity = 0.2;    
        params.filterByInertia = true;
        params.maxInertiaRatio = 0.85;

        SimpleBlobDetector detector(params); 

        std::vector<KeyPoint> keypointsred,keypointsblue,keypointsgreen;


        detector.detect( srcred, keypointsred);
        detector.detect( srcblue, keypointsblue);
        detector.detect( srcgreen, keypointsgreen);
       



        if(currStatus==READING)
        {
            Mat im_with_keypoints_red,im_with_keypoints_white,im_with_keypoints_green,im_with_keypoints_blue;

        
            drawKeypoints( detectionImg, keypointsred, im_with_keypoints_red, Scalar(0,0,255), DrawMatchesFlags::DRAW_RICH_KEYPOINTS );
            drawKeypoints( detectionImg, keypointsblue, im_with_keypoints_blue, Scalar(0,0,255), DrawMatchesFlags::DRAW_RICH_KEYPOINTS );
            drawKeypoints( detectionImg, keypointsgreen, im_with_keypoints_green, Scalar(0,0,255), DrawMatchesFlags::DRAW_RICH_KEYPOINTS );
            imshow("keypointsR", im_with_keypoints_red);
            imshow("keypointsG", im_with_keypoints_green);
            imshow("keypointsB", im_with_keypoints_blue);
        }

        cv:waitKey(5);


        // imshow("Original",detectionImg);

        if(currStatus==READING)
        {
            int a=detectPoster(keypointsred,keypointsgreen,keypointsblue);
            returned[4]=returned[3];
            returned[3]=returned[2];
            returned[2]=returned[1];
            returned[1]=returned[0];
            returned[0]=a;
            if(returned[4]==returned[3] && returned[3]==returned[2] && returned[2]==returned[1] && returned[1]==returned[0] && returned[1]!=-1)
            {

                angleArrayLock.lock();
                if(fabs(angles[returned[2]])<0.01)
                {
                    if(returned[2]==(prevAngle+1)%NUMLABELS || returned[2]==(prevAngle-1)%NUMLABELS || prevAngle==-1)
                    {
                        angles[returned[2]]=currAngle;
                        cout<<returned[2]<<endl;
                        prevAngle=returned[2];
                    }

                }        
                angleArrayLock.unlock();
            }
        }
        else if(currStatus==HOMING)
        {
            int count=0;
            vector<int> goodMatches; //detected label numbers
            vector<int> xValues; //xValues of the labels
           
            start: 
 
            if(count==3)
            {
                line(detectionImgFull,Point(xValues[0], 20),Point(xValues[0], 340),Scalar(255,0,0),1,8,0); //to identify where labels are being detected
                line(detectionImgFull,Point(xValues[1], 20),Point(xValues[1], 340),Scalar(255,0,0),1,8,0);
                line(detectionImgFull,Point(xValues[2], 20),Point(xValues[2], 340),Scalar(255,0,0),1,8,0);

                putText(detectionImgFull, to_string(goodMatches[0]), Point(xValues[0], 120), FONT_HERSHEY_PLAIN, 2, Scalar::all(255), 3, 8); //to identify what labels are being detected
                putText(detectionImgFull, to_string(goodMatches[1]), Point(xValues[1], 120), FONT_HERSHEY_PLAIN, 2, Scalar::all(255), 3, 8);
                putText(detectionImgFull, to_string(goodMatches[2]), Point(xValues[2], 120), FONT_HERSHEY_PLAIN, 2, Scalar::all(255), 3, 8);


                imshow("labelDetection",detectionImgFull); 


                sort(goodMatches.begin(),goodMatches.end(),myfunction); //myfunction called only for this
                sort(xValues.begin(),xValues.end(),myfunction2); //regular sorting using myfunction2


                


                
                if((goodMatches[0]+1)%NUMLABELS==goodMatches[1] && (goodMatches[1]+1)%NUMLABELS==goodMatches[2]) //only if 3 consecutive ones are encountered
                {
                    //cout<<goodMatches[0]<<" "<<goodMatches[1]<<" "<<goodMatches[2]<<endl;
                    angleArrayLock.lock();
                    currAngleLock.lock();

                    thetasMessage.angular.x = angles[goodMatches[0]]*PI/180.0; //storing data in message
                    thetasMessage.angular.y = angles[goodMatches[1]]*PI/180.0; 
                    thetasMessage.angular.z = angles[goodMatches[2]]*PI/180.0; 

                    float linearx = (currAngle + (xValues[0]-320.0)*0.14375)*PI/180.0;
                    float lineary = (currAngle + (xValues[1]-320.0)*0.14375)*PI/180.0;
                    float linearz = (currAngle + (xValues[2]-320.0)*0.14375)*PI/180.0;

                    thetasMessage.linear.x = atan2(sin(linearx) , cos(linearx));
                    thetasMessage.linear.y = atan2(sin(lineary) , cos(lineary));
                    thetasMessage.linear.z = atan2(sin(linearz) , cos(linearz));

                    flyingMessage.data=1; //3 correct labels are identified, follow path 

                    currAngleLock.unlock();
                    angleArrayLock.unlock();
                    
                    return;
                }

                else
                {
                    thetasMessage.angular.x = 5;
                    thetasMessage.angular.y = 5;
                    thetasMessage.angular.z = 5;
                    thetasMessage.linear.x = 5;
                    thetasMessage.linear.y = 5;
                    thetasMessage.linear.z = 5;
                    flyingMessage.data=2; //3 labels are detected, but incorrectly. wait for correct labels.
                    
                    return;
                }
                
            }

//really long set of for loops to find labels amidst all keypoints.
            for(int i=0; i<keypointsred.size();i++)
            {
                for(int j=0;j<keypointsgreen.size();j++)
                {
                    if(dist(keypointsred[i].pt,keypointsgreen[j].pt)<6*keypointsgreen[j].size)
                    {   
                        for(int k=0;k<keypointsblue.size();k++)
                        {
                            if(dist(keypointsblue[k].pt,keypointsgreen[j].pt)<6*keypointsgreen[j].size)
                            {
                                if(keypointsblue[k].pt.y > keypointsgreen[j].pt.y && keypointsblue[k].pt.y > keypointsred[i].pt.y && keypointsgreen[j].pt.y > keypointsred[i].pt.y)
                                {
                                    goodMatches.push_back(1);
                                    xValues.push_back(keypointsgreen[j].pt.x);
                                    keypointsred.erase(keypointsred.begin()+i);
                                    keypointsgreen.erase(keypointsgreen.begin()+j);
                                    keypointsblue.erase(keypointsblue.begin()+k);
                                    //cout<<"RGB"<<endl;
                                    count++;
                                    goto start;
                                }
                                else if(keypointsblue[k].pt.y > keypointsgreen[j].pt.y && keypointsblue[k].pt.y > keypointsred[i].pt.y && keypointsred[i].pt.y > keypointsgreen[j].pt.y)
                                {
                                    goodMatches.push_back(3);
                                    xValues.push_back(keypointsred[i].pt.x);

                                    keypointsred.erase(keypointsred.begin()+i);
                                    keypointsgreen.erase(keypointsgreen.begin()+j);
                                    keypointsblue.erase(keypointsblue.begin()+k);
                                    //cout<<"GRB"<<endl;
                                    count++;
                                    goto start;
                                }
                                else if(keypointsgreen[j].pt.y > keypointsblue[k].pt.y && keypointsgreen[j].pt.y > keypointsred[i].pt.y && keypointsblue[k].pt.y > keypointsred[i].pt.y)
                                {
                                    goodMatches.push_back(13);
                                    xValues.push_back(keypointsblue[k].pt.x);

                                    keypointsred.erase(keypointsred.begin()+i);
                                    keypointsgreen.erase(keypointsgreen.begin()+j);
                                    keypointsblue.erase(keypointsblue.begin()+k);
                                    //cout<<"RBG"<<endl;
                                    count++;
                                    goto start;
                                }
                                else if(keypointsgreen[j].pt.y > keypointsblue[k].pt.y && keypointsgreen[j].pt.y > keypointsred[i].pt.y && keypointsred[i].pt.y > keypointsblue[k].pt.y)
                                {
                                    goodMatches.push_back(4);
                                    xValues.push_back(keypointsred[i].pt.x);

                                    keypointsred.erase(keypointsred.begin()+i);
                                    keypointsgreen.erase(keypointsgreen.begin()+j);
                                    keypointsblue.erase(keypointsblue.begin()+k);
                                    //cout<<"BRG"<<endl;
                                    count++;
                                    goto start;
                                }
                                else if(keypointsred[i].pt.y > keypointsblue[k].pt.y &&  keypointsred[i].pt.y > keypointsgreen[j].pt.y && keypointsgreen[j].pt.y > keypointsblue[k].pt.y)
                                {
                                    goodMatches.push_back(6);
                                    xValues.push_back(keypointsgreen[j].pt.x);

                                    keypointsred.erase(keypointsred.begin()+i);
                                    keypointsgreen.erase(keypointsgreen.begin()+j);
                                    keypointsblue.erase(keypointsblue.begin()+k);
                                    //cout<<"BGR"<<endl;
                                    count++;
                                    goto start;
                                }
                                else if(keypointsred[i].pt.y > keypointsblue[k].pt.y &&  keypointsred[i].pt.y > keypointsgreen[j].pt.y && keypointsblue[k].pt.y > keypointsgreen[j].pt.y)
                                {
                                    goodMatches.push_back(9);
                                    xValues.push_back(keypointsblue[k].pt.x); 

                                    keypointsred.erase(keypointsred.begin()+i);
                                    keypointsgreen.erase(keypointsgreen.begin()+j);
                                    keypointsblue.erase(keypointsblue.begin()+k);
                                    //cout<<"GBR"<<endl;
                                    count++;
                                    goto start;
                                }

                            }

                        }
                        if(keypointsred[i].pt.y > keypointsgreen[j].pt.y)
                        {   
                            goodMatches.push_back(11);
                            xValues.push_back(keypointsred[i].pt.x); 

                            keypointsred.erase(keypointsred.begin()+i);
                            keypointsgreen.erase(keypointsgreen.begin()+j);
                            //cout<<"WGR"<<endl;
                            count++;
                            goto start;
                        }
                        else if(keypointsgreen[j].pt.y > keypointsred[i].pt.y)
                        {
                            if(dist(keypointsgreen[j].pt,keypointsred[i].pt)<2*keypointsgreen[j].size)
                            {
                                goodMatches.push_back(8);
                                xValues.push_back(keypointsred[i].pt.x); 
                                //cout<<"WRG"<<endl;
                            }
                            else
                            {
                                goodMatches.push_back(15);
                                xValues.push_back(keypointsred[i].pt.x); 
                                //cout<<"RWG"<<endl;
                            }

                            keypointsred.erase(keypointsred.begin()+i);
                            keypointsgreen.erase(keypointsgreen.begin()+j);
                            

                            count++;
                            goto start;
                        }
                    }
                }
                for(int k=0;k<keypointsblue.size();k++)
                {
                    if(dist(keypointsred[i].pt,keypointsblue[k].pt)<6*keypointsblue[k].size)
                    {   

                        if(keypointsred[i].pt.y > keypointsblue[k].pt.y)
                        {   

                            if(dist(keypointsblue[k].pt,keypointsred[i].pt)<2.5*keypointsblue[k].size)
                            {
                                goodMatches.push_back(7);
                                xValues.push_back(keypointsred[i].pt.x); 
                                //cout<<"WBR"<<endl;
                            }
                            else
                            {
                                goodMatches.push_back(10);
                                xValues.push_back(keypointsred[i].pt.x); 
                                //cout<<"BWR"<<endl;
                            }

                            keypointsred.erase(keypointsred.begin()+i);
                            keypointsblue.erase(keypointsblue.begin()+k);

                            count++;
                            goto start;
                        }
                        else if(keypointsblue[k].pt.y > keypointsred[i].pt.y)
                        {
                            if(dist(keypointsblue[k].pt,keypointsred[i].pt)<2.5*keypointsblue[k].size)
                            {
                                goodMatches.push_back(5);
                                xValues.push_back(keypointsred[i].pt.x);
                                //cout<<"WRB"<<endl; 
                            }
                            else
                            {
                                goodMatches.push_back(0);
                                xValues.push_back(keypointsred[i].pt.x);
                                //cout<<"RWB"<<endl;
                            }
                            keypointsred.erase(keypointsred.begin()+i);
                            keypointsblue.erase(keypointsblue.begin()+k);

                            count++;
                            goto start;
                        }
                    }
                }
            }
            for(int j=0;j<keypointsgreen.size();j++)
            {
                for(int k=0;k<keypointsblue.size();k++)
                {
                    if(dist(keypointsblue[k].pt,keypointsgreen[j].pt)<6*keypointsgreen[j].size)
                    {
                        if(keypointsblue[k].pt.y > keypointsgreen[j].pt.y)
                        { 
                            if(dist(keypointsgreen[j].pt,keypointsblue[k].pt)<2*keypointsgreen[j].size)
                            {
                                goodMatches.push_back(12);
                                xValues.push_back(keypointsgreen[j].pt.x); 
                                //cout<<"WGB"<<endl;
                            }
                            else
                            {
                                goodMatches.push_back(2);
                                xValues.push_back(keypointsgreen[j].pt.x); 
                                //cout<<"GWB"<<endl;
                            }
                            keypointsblue.erase(keypointsblue.begin()+k);
                            keypointsgreen.erase(keypointsgreen.begin()+j);
                            count++;
                            goto start;
                        }
                        else if(keypointsgreen[j].pt.y > keypointsblue[k].pt.y)
                        {
                            goodMatches.push_back(14);
                            xValues.push_back(keypointsblue[k].pt.x); 

                            keypointsgreen.erase(keypointsgreen.begin()+j);
                            keypointsblue.erase(keypointsblue.begin()+k);
                            //cout<<"WBG"<<endl;
                            count++;
                            goto start;
                        }
                    }
                }
            }
        }

        thetasMessage.angular.x = 10;
        thetasMessage.angular.y = 10;
        thetasMessage.angular.z = 10;
        thetasMessage.linear.x = 10;
        thetasMessage.linear.y = 10;
        thetasMessage.linear.z = 10;
        flyingMessage.data=3; //3 labels never encountered.
        return;

}




int main(int argc, char **argv)
{
  
  ros::init(argc, argv, "stitch_node");  //stitch_node is the node name.

  ros::NodeHandle nh;
  ros::Subscriber statusSub = nh.subscribe <std_msgs::Int32>("homingStatus", 100, statusCallBack); //homing status defines the state of the code, 0 = IDLE, 1= READING, 2=HOMING
  image_transport::ImageTransport it(nh); //for communicating image messages
  image_transport::Subscriber sub = it.subscribe("ardrone/front/image_raw", 10, imageCallback); 
  ros::Subscriber magSub = nh.subscribe <geometry_msgs::Vector3Stamped>("/ardrone/mag", 100, magCallBack); 
  ros::Publisher homingTwistPub = nh.advertise <geometry_msgs::Twist>("thetaAngles",100); //it publishes the angles wrt north
  ros::Publisher flyingStatusPub = nh.advertise <std_msgs::Int32>("flyingStatus",100); //publish the state of label detection
  ros::Rate loop_rate(20);


  while(ros::ok())
  {
    ofstream myfile;
    angleArrayLock.lock();
    if(currStatus==READING || currStatus==IDLE && haveRead==0)
    {
        if(isnonzero(angles,NUMLABELS))
        {
            myfile.open("/home/thesidjway/ardrone_ws/src/angles.txt", ios::out);
            myfile<<"";
            myfile.close();
            myfile.open("/home/thesidjway/ardrone_ws/src/angles.txt", ios::app); //update the angles everytime reading is complete.
            for(int i=0;i<NUMLABELS;i++)
            {
                if((int)angles[i]+360>=100)
                    myfile<<(int)angles[i]+360<<",";
                else if((int)angles[i]+360>=10)
                    myfile<<"0"<<(int)angles[i]+360<<",";
                else
                    myfile<<"00"<<(int)angles[i]+360<<",";

            }
            myfile.close();
        }
    }
    //cout<<angles[0]<<" "<<angles[1]<<" "<<angles[2]<<" "<<angles[3]<<" "<<angles[4]<<" "<<angles[5]<<" "<<angles[6]<<" "<<angles[7]<<" "<<angles[8]<<" "<<angles[9]<<" "<<angles[10]<<" "<<angles[11]<<" "<<angles[12]<<" "<<angles[13]<<endl;
    angleArrayLock.unlock();
    if(currStatus==HOMING)
    {
        homingTwistPub.publish(thetasMessage);
        flyingStatusPub.publish(flyingMessage); //required only when homing is active.
    }
    
    ros::spinOnce();
  }
  return 0;
}