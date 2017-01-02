#include "PhysicsClientSharedMemory.h"
#include "PosixSharedMemory.h"
#include "Win32SharedMemory.h"
#include "LinearMath/btAlignedObjectArray.h"
#include "LinearMath/btVector3.h"
#include <string.h>

#include "Bullet3Common/b3Logging.h"
#include "../Utils/b3ResourcePath.h"
#include "../../Extras/Serialize/BulletFileLoader/btBulletFile.h"
#include "../../Extras/Serialize/BulletFileLoader/autogenerated/bullet.h"
#include "SharedMemoryBlock.h"
#include "BodyJointInfoUtility.h"





struct BodyJointInfoCache
{
	std::string m_baseName;
	btAlignedObjectArray<b3JointInfo> m_jointInfo;
};

struct PhysicsClientSharedMemoryInternalData {
    SharedMemoryInterface* m_sharedMemory;
	bool	m_ownsSharedMemory;
    SharedMemoryBlock* m_testBlock1;
   
	btHashMap<btHashInt,BodyJointInfoCache*> m_bodyJointMap;

    btAlignedObjectArray<TmpFloat3> m_debugLinesFrom;
    btAlignedObjectArray<TmpFloat3> m_debugLinesTo;
    btAlignedObjectArray<TmpFloat3> m_debugLinesColor;

	int m_cachedCameraPixelsWidth;
	int m_cachedCameraPixelsHeight;
	btAlignedObjectArray<unsigned char> m_cachedCameraPixelsRGBA;
	btAlignedObjectArray<float> m_cachedCameraDepthBuffer;
	btAlignedObjectArray<int> m_cachedSegmentationMaskBuffer;

    btAlignedObjectArray<b3ContactPointData> m_cachedContactPoints;
	btAlignedObjectArray<b3OverlappingObject> m_cachedOverlappingObjects;
	btAlignedObjectArray<b3VisualShapeData> m_cachedVisualShapes;

    btAlignedObjectArray<int> m_bodyIdsRequestInfo;
    SharedMemoryStatus m_tempBackupServerStatus;
    
    SharedMemoryStatus m_lastServerStatus;

    int m_counter;
    
    bool m_isConnected;
    bool m_waitingForServer;
    bool m_hasLastServerStatus;
    int m_sharedMemoryKey;
    bool m_verboseOutput;

    PhysicsClientSharedMemoryInternalData()
        : m_sharedMemory(0),
		  m_ownsSharedMemory(false),
          m_testBlock1(0),
		  m_counter(0),
		  m_cachedCameraPixelsWidth(0),
		  m_cachedCameraPixelsHeight(0),
		  
          m_isConnected(false),
          m_waitingForServer(false),
          m_hasLastServerStatus(false),
          m_sharedMemoryKey(SHARED_MEMORY_KEY),
          m_verboseOutput(false) {}

    void processServerStatus();

    bool canSubmitCommand() const;
};



int PhysicsClientSharedMemory::getNumBodies() const
{
	return m_data->m_bodyJointMap.size();
}

int PhysicsClientSharedMemory::getBodyUniqueId(int serialIndex) const
{
	if ((serialIndex >= 0) && (serialIndex < getNumBodies()))
	{
		return m_data->m_bodyJointMap.getKeyAtIndex(serialIndex).getUid1();
	}
	return -1;
}

bool PhysicsClientSharedMemory::getBodyInfo(int bodyUniqueId, struct b3BodyInfo& info) const
{
	BodyJointInfoCache** bodyJointsPtr = m_data->m_bodyJointMap[bodyUniqueId];
	if (bodyJointsPtr && *bodyJointsPtr)
	{
		BodyJointInfoCache* bodyJoints = *bodyJointsPtr;
		info.m_baseName = bodyJoints->m_baseName.c_str();
		return true;
	}




	return false;
}



int PhysicsClientSharedMemory::getNumJoints(int bodyUniqueId) const 
{
	BodyJointInfoCache** bodyJointsPtr = m_data->m_bodyJointMap[bodyUniqueId];
	if (bodyJointsPtr && *bodyJointsPtr)
	{
		BodyJointInfoCache* bodyJoints = *bodyJointsPtr;

		return bodyJoints->m_jointInfo.size(); 
	}
	return 0;
	
}

bool PhysicsClientSharedMemory::getJointInfo(int bodyUniqueId, int jointIndex, b3JointInfo& info) const
{
	BodyJointInfoCache** bodyJointsPtr = m_data->m_bodyJointMap[bodyUniqueId];
	if (bodyJointsPtr && *bodyJointsPtr)
	{
		BodyJointInfoCache* bodyJoints = *bodyJointsPtr;
		if ((jointIndex >= 0) && (jointIndex < bodyJoints->m_jointInfo.size()))
		{
			info = bodyJoints->m_jointInfo[jointIndex];
			return true;
		}
	}
    return false;
}

PhysicsClientSharedMemory::PhysicsClientSharedMemory()

{
    m_data = new PhysicsClientSharedMemoryInternalData;

#ifdef _WIN32
    m_data->m_sharedMemory = new Win32SharedMemoryClient();
#else
    m_data->m_sharedMemory = new PosixSharedMemory();
#endif
	m_data->m_ownsSharedMemory = true;

}

PhysicsClientSharedMemory::~PhysicsClientSharedMemory() {
    if (m_data->m_isConnected) {
        disconnectSharedMemory();
    }
	if (m_data->m_ownsSharedMemory)
	{
	    delete m_data->m_sharedMemory;
	}
    delete m_data;
}

void PhysicsClientSharedMemory::setSharedMemoryKey(int key) { m_data->m_sharedMemoryKey = key; }


void PhysicsClientSharedMemory::setSharedMemoryInterface(class SharedMemoryInterface* sharedMem)
{
	if (m_data->m_sharedMemory && m_data->m_ownsSharedMemory)
	{
		delete m_data->m_sharedMemory;
	}
	m_data->m_ownsSharedMemory = false;
	m_data->m_sharedMemory = sharedMem;
}

void PhysicsClientSharedMemory::disconnectSharedMemory() {
    if (m_data->m_isConnected && m_data->m_sharedMemory) {
        m_data->m_sharedMemory->releaseSharedMemory(m_data->m_sharedMemoryKey, SHARED_MEMORY_SIZE);
    }
	m_data->m_isConnected = false;

}

bool PhysicsClientSharedMemory::isConnected() const { return m_data->m_isConnected; }

bool PhysicsClientSharedMemory::connect() {
    /// server always has to create and initialize shared memory
    bool allowCreation = false;
    m_data->m_testBlock1 = (SharedMemoryBlock*)m_data->m_sharedMemory->allocateSharedMemory(
        m_data->m_sharedMemoryKey, SHARED_MEMORY_SIZE, allowCreation);

    if (m_data->m_testBlock1) {
        if (m_data->m_testBlock1->m_magicId != SHARED_MEMORY_MAGIC_NUMBER) {
            b3Error("Error: please start server before client\n");
            m_data->m_sharedMemory->releaseSharedMemory(m_data->m_sharedMemoryKey,
                                                        SHARED_MEMORY_SIZE);
            m_data->m_testBlock1 = 0;
            return false;
        } else {
            if (m_data->m_verboseOutput) {
                b3Printf("Connected to existing shared memory, status OK.\n");
            }
            m_data->m_isConnected = true;
        }
    } else {
        b3Error("Cannot connect to shared memory");
        return false;
    }
#if 0
	if (m_data->m_isConnected)
	{
		//get all existing bodies and body info...

		SharedMemoryCommand& command = m_data->m_testBlock1->m_clientCommands[0];
		//now transfer the information of the individual objects etc.
		command.m_type = CMD_REQUEST_BODY_INFO;
		command.m_sdfRequestInfoArgs.m_bodyUniqueId = 37;
		submitClientCommand(command);
		int timeout = 1024 * 1024 * 1024;
		
		const SharedMemoryStatus* status = 0;

		while ((status == 0) && (timeout-- > 0))
		{
			status = processServerStatus();
		
		}


		//submitClientCommand(command);


	}
#endif
    return true;
}


///todo(erwincoumans) refactor this: merge with PhysicsDirect::processBodyJointInfo
void PhysicsClientSharedMemory::processBodyJointInfo(int bodyUniqueId, const SharedMemoryStatus& serverCmd)
{
    bParse::btBulletFile bf(
                            &this->m_data->m_testBlock1->m_bulletStreamDataServerToClientRefactor[0],
                            serverCmd.m_numDataStreamBytes);
    bf.setFileDNAisMemoryDNA();
    bf.parse(false);
    
    
    BodyJointInfoCache* bodyJoints = new BodyJointInfoCache;
    m_data->m_bodyJointMap.insert(bodyUniqueId,bodyJoints);
    
    for (int i = 0; i < bf.m_multiBodies.size(); i++)
    {
        int flag = bf.getFlags();
        if ((flag & bParse::FD_DOUBLE_PRECISION) != 0)
        {
            Bullet::btMultiBodyDoubleData* mb =
            (Bullet::btMultiBodyDoubleData*)bf.m_multiBodies[i];
            
			bodyJoints->m_baseName = mb->m_baseName;
            addJointInfoFromMultiBodyData(mb,bodyJoints, m_data->m_verboseOutput);
        } else
        {
            Bullet::btMultiBodyFloatData* mb =
            (Bullet::btMultiBodyFloatData*)bf.m_multiBodies[i];
			bodyJoints->m_baseName = mb->m_baseName;
            addJointInfoFromMultiBodyData(mb,bodyJoints, m_data->m_verboseOutput);
        }
    }
    if (bf.ok()) {
        if (m_data->m_verboseOutput)
        {
            b3Printf("Received robot description ok!\n");
        }
    } else
    {
        b3Warning("Robot description not received");
    }
}

const SharedMemoryStatus* PhysicsClientSharedMemory::processServerStatus() {
    SharedMemoryStatus* stat = 0;

    if (!m_data->m_testBlock1) {
		m_data->m_lastServerStatus.m_type = CMD_SHARED_MEMORY_NOT_INITIALIZED;
		return &m_data->m_lastServerStatus;
    }

    if (!m_data->m_waitingForServer) {
        return 0;
    }

	 if (m_data->m_testBlock1->m_magicId != SHARED_MEMORY_MAGIC_NUMBER) 
	 {
		 m_data->m_lastServerStatus.m_type = CMD_SHARED_MEMORY_NOT_INITIALIZED;
		 return &m_data->m_lastServerStatus;
	 }

    if (m_data->m_testBlock1->m_numServerCommands >
        m_data->m_testBlock1->m_numProcessedServerCommands) {
        btAssert(m_data->m_testBlock1->m_numServerCommands ==
                 m_data->m_testBlock1->m_numProcessedServerCommands + 1);

        const SharedMemoryStatus& serverCmd = m_data->m_testBlock1->m_serverCommands[0];
        m_data->m_lastServerStatus = serverCmd;

        EnumSharedMemoryServerStatus s = (EnumSharedMemoryServerStatus)serverCmd.m_type;
        // consume the command

        switch (serverCmd.m_type) {
            case CMD_CLIENT_COMMAND_COMPLETED: {
                if (m_data->m_verboseOutput) {
                    b3Printf("Server completed command");
                }
                break;
            }
            case CMD_SDF_LOADING_COMPLETED: {
                
                if (m_data->m_verboseOutput) {
                    b3Printf("Server loading the SDF OK\n");
                }

                break;
            }

            case CMD_URDF_LOADING_COMPLETED: {
                
                if (m_data->m_verboseOutput) {
                    b3Printf("Server loading the URDF OK\n");
                }

                if (serverCmd.m_numDataStreamBytes > 0) {
                    bParse::btBulletFile bf(
                        this->m_data->m_testBlock1->m_bulletStreamDataServerToClientRefactor,
                        serverCmd.m_numDataStreamBytes);
                    bf.setFileDNAisMemoryDNA();
                    bf.parse(false);
					int bodyUniqueId = serverCmd.m_dataStreamArguments.m_bodyUniqueId;

					BodyJointInfoCache* bodyJoints = new BodyJointInfoCache;
                    m_data->m_bodyJointMap.insert(bodyUniqueId,bodyJoints);

                    for (int i = 0; i < bf.m_multiBodies.size(); i++) {


                        int flag = bf.getFlags();
                        
                        if ((flag & bParse::FD_DOUBLE_PRECISION) != 0) {
                            Bullet::btMultiBodyDoubleData* mb =
                                (Bullet::btMultiBodyDoubleData*)bf.m_multiBodies[i];

							addJointInfoFromMultiBodyData(mb,bodyJoints, m_data->m_verboseOutput);
                        } else 
						{
                            Bullet::btMultiBodyFloatData* mb =
                                (Bullet::btMultiBodyFloatData*)bf.m_multiBodies[i];

							addJointInfoFromMultiBodyData(mb,bodyJoints, m_data->m_verboseOutput);
                        }
                    }
                    if (bf.ok()) {
                        if (m_data->m_verboseOutput) {
                            b3Printf("Received robot description ok!\n");
                        }
                    } else {
                        b3Warning("Robot description not received");
                    }
                }
                break;
            }
            case CMD_DESIRED_STATE_RECEIVED_COMPLETED: {
                if (m_data->m_verboseOutput) {
                    b3Printf("Server received desired state");
                }
                break;
            }
            case CMD_STEP_FORWARD_SIMULATION_COMPLETED: {
                if (m_data->m_verboseOutput) {
                    b3Printf("Server completed step simulation");
                }
                break;
            }
            case CMD_URDF_LOADING_FAILED: {
                if (m_data->m_verboseOutput) {
                    b3Printf("Server failed loading the URDF...\n");
                }
                
                break;
            }
            
            case CMD_BODY_INFO_COMPLETED:
            {
                if (m_data->m_verboseOutput) {
                    b3Printf("Received body info\n");
                }
                int bodyUniqueId = serverCmd.m_dataStreamArguments.m_bodyUniqueId;
                processBodyJointInfo(bodyUniqueId, serverCmd);

                break;
            }
             case CMD_SDF_LOADING_FAILED: {
                if (m_data->m_verboseOutput) {
                    b3Printf("Server failed loading the SDF...\n");
                }
                
                break;
            }

            case CMD_BULLET_DATA_STREAM_RECEIVED_COMPLETED: {
                if (m_data->m_verboseOutput) {
                    b3Printf("Server received bullet data stream OK\n");
                }

                break;
            }
            case CMD_BULLET_DATA_STREAM_RECEIVED_FAILED: {
                if (m_data->m_verboseOutput) {
                    b3Printf("Server failed receiving bullet data stream\n");
                }

                break;
            }

            case CMD_ACTUAL_STATE_UPDATE_COMPLETED: {
                if (m_data->m_verboseOutput) {
                    b3Printf("Received actual state\n");
                }
                SharedMemoryStatus& command = m_data->m_testBlock1->m_serverCommands[0];

                int numQ = command.m_sendActualStateArgs.m_numDegreeOfFreedomQ;
                int numU = command.m_sendActualStateArgs.m_numDegreeOfFreedomU;
                if (m_data->m_verboseOutput) {
                    b3Printf("size Q = %d, size U = %d\n", numQ, numU);
                }
                char msg[1024];

                {
                    sprintf(msg, "Q=[");

                    for (int i = 0; i < numQ; i++) {
                        if (i < numQ - 1) {
                            sprintf(msg, "%s%f,", msg,
                                    command.m_sendActualStateArgs.m_actualStateQ[i]);
                        } else {
                            sprintf(msg, "%s%f", msg,
                                    command.m_sendActualStateArgs.m_actualStateQ[i]);
                        }
                    }
                    sprintf(msg, "%s]", msg);
                }
                if (m_data->m_verboseOutput) {
                    b3Printf(msg);
                }

                {
                    sprintf(msg, "U=[");

                    for (int i = 0; i < numU; i++) {
                        if (i < numU - 1) {
                            sprintf(msg, "%s%f,", msg,
                                    command.m_sendActualStateArgs.m_actualStateQdot[i]);
                        } else {
                            sprintf(msg, "%s%f", msg,
                                    command.m_sendActualStateArgs.m_actualStateQdot[i]);
                        }
                    }
                    sprintf(msg, "%s]", msg);
                }
                if (m_data->m_verboseOutput) {
                    b3Printf(msg);
                }

                if (m_data->m_verboseOutput) {
                    b3Printf("\n");
                }
                break;
            }
            case CMD_RESET_SIMULATION_COMPLETED: {
                if (m_data->m_verboseOutput) {
                    b3Printf("CMD_RESET_SIMULATION_COMPLETED clean data\n");
                }
				m_data->m_debugLinesFrom.clear();
				m_data->m_debugLinesTo.clear();
				m_data->m_debugLinesColor.clear();
                for (int i=0;i<m_data->m_bodyJointMap.size();i++)
				{
					BodyJointInfoCache** bodyJointsPtr = m_data->m_bodyJointMap.getAtIndex(i);
					if (bodyJointsPtr && *bodyJointsPtr)
					{
						BodyJointInfoCache* bodyJoints = *bodyJointsPtr;
						for (int j=0;j<bodyJoints->m_jointInfo.size();j++) {
							if (bodyJoints->m_jointInfo[j].m_jointName)
							{
								free(bodyJoints->m_jointInfo[j].m_jointName);
							}
							if (bodyJoints->m_jointInfo[j].m_linkName)
							{
								free(bodyJoints->m_jointInfo[j].m_linkName);
							}
						}
						delete (*bodyJointsPtr);
					}
				}
				m_data->m_bodyJointMap.clear();
                
                break;
            }
            case CMD_DEBUG_LINES_COMPLETED: {
                if (m_data->m_verboseOutput) {
                    b3Printf("Success receiving %d debug lines",
                             serverCmd.m_sendDebugLinesArgs.m_numDebugLines);
                }

                int numLines = serverCmd.m_sendDebugLinesArgs.m_numDebugLines;
                float* linesFrom =
                    (float*)&m_data->m_testBlock1->m_bulletStreamDataServerToClientRefactor[0];
                float* linesTo =
                    (float*)(&m_data->m_testBlock1->m_bulletStreamDataServerToClientRefactor[0] +
                             numLines * 3 * sizeof(float));
                float* linesColor =
                    (float*)(&m_data->m_testBlock1->m_bulletStreamDataServerToClientRefactor[0] +
                             2 * numLines * 3 * sizeof(float));

                m_data->m_debugLinesFrom.resize(serverCmd.m_sendDebugLinesArgs.m_startingLineIndex +
                                                numLines);
                m_data->m_debugLinesTo.resize(serverCmd.m_sendDebugLinesArgs.m_startingLineIndex +
                                              numLines);
                m_data->m_debugLinesColor.resize(
                    serverCmd.m_sendDebugLinesArgs.m_startingLineIndex + numLines);

                for (int i = 0; i < numLines; i++) {
                    TmpFloat3 from = CreateTmpFloat3(linesFrom[i * 3], linesFrom[i * 3 + 1],
                                                     linesFrom[i * 3 + 2]);
                    TmpFloat3 to =
                        CreateTmpFloat3(linesTo[i * 3], linesTo[i * 3 + 1], linesTo[i * 3 + 2]);
                    TmpFloat3 color = CreateTmpFloat3(linesColor[i * 3], linesColor[i * 3 + 1],
                                                      linesColor[i * 3 + 2]);

                    m_data
                        ->m_debugLinesFrom[serverCmd.m_sendDebugLinesArgs.m_startingLineIndex + i] =
                        from;
                    m_data->m_debugLinesTo[serverCmd.m_sendDebugLinesArgs.m_startingLineIndex + i] =
                        to;
                    m_data->m_debugLinesColor[serverCmd.m_sendDebugLinesArgs.m_startingLineIndex +
                                              i] = color;
                }

                break;
            }
			case CMD_RIGID_BODY_CREATION_COMPLETED:
			{

				break;
			}
            case CMD_DEBUG_LINES_OVERFLOW_FAILED: {
                b3Warning("Error receiving debug lines");
                m_data->m_debugLinesFrom.resize(0);
                m_data->m_debugLinesTo.resize(0);
                m_data->m_debugLinesColor.resize(0);

                break;
            }
            
            case CMD_CAMERA_IMAGE_COMPLETED:
            {
				if (m_data->m_verboseOutput) 
				{
					b3Printf("Camera image OK\n");
				}

				int numBytesPerPixel = 4;//RGBA
				int numTotalPixels = serverCmd.m_sendPixelDataArguments.m_startingPixelIndex+
					serverCmd.m_sendPixelDataArguments.m_numPixelsCopied+
					serverCmd.m_sendPixelDataArguments.m_numRemainingPixels;

				m_data->m_cachedCameraPixelsWidth = 0;
				m_data->m_cachedCameraPixelsHeight = 0;

                int numPixels = serverCmd.m_sendPixelDataArguments.m_imageWidth*serverCmd.m_sendPixelDataArguments.m_imageHeight;

                m_data->m_cachedCameraPixelsRGBA.reserve(numPixels*numBytesPerPixel);
				m_data->m_cachedCameraDepthBuffer.resize(numTotalPixels);
				m_data->m_cachedSegmentationMaskBuffer.resize(numTotalPixels);
				m_data->m_cachedCameraPixelsRGBA.resize(numTotalPixels*numBytesPerPixel);
                
                
				unsigned char* rgbaPixelsReceived =
                    (unsigned char*)&m_data->m_testBlock1->m_bulletStreamDataServerToClientRefactor[0];
              //  printf("pixel = %d\n", rgbaPixelsReceived[0]);
                
				float* depthBuffer = (float*)&(m_data->m_testBlock1->m_bulletStreamDataServerToClientRefactor[serverCmd.m_sendPixelDataArguments.m_numPixelsCopied*4]);
				int* segmentationMaskBuffer = (int*)&(m_data->m_testBlock1->m_bulletStreamDataServerToClientRefactor[serverCmd.m_sendPixelDataArguments.m_numPixelsCopied*8]);
			
				for (int i=0;i<serverCmd.m_sendPixelDataArguments.m_numPixelsCopied;i++)
				{
					m_data->m_cachedCameraDepthBuffer[i + serverCmd.m_sendPixelDataArguments.m_startingPixelIndex] = depthBuffer[i];
				}
				
				for (int i=0;i<serverCmd.m_sendPixelDataArguments.m_numPixelsCopied;i++)
				{
					m_data->m_cachedSegmentationMaskBuffer[i + serverCmd.m_sendPixelDataArguments.m_startingPixelIndex] = segmentationMaskBuffer[i];
				}

				for (int i=0;i<serverCmd.m_sendPixelDataArguments.m_numPixelsCopied*numBytesPerPixel;i++)
				{
					m_data->m_cachedCameraPixelsRGBA[i + serverCmd.m_sendPixelDataArguments.m_startingPixelIndex*numBytesPerPixel] 
						= rgbaPixelsReceived[i];
				}

                break;
            } 
            
            case CMD_CAMERA_IMAGE_FAILED:
            {
                b3Warning("Camera image FAILED\n");
                break;
            }
			case CMD_CALCULATED_INVERSE_DYNAMICS_COMPLETED:
			{
				break;
			}
			case CMD_CALCULATED_INVERSE_DYNAMICS_FAILED:
			{
				b3Warning("Inverse Dynamics computations failed");
				break;
			}
			case CMD_REQUEST_AABB_OVERLAP_FAILED:
			{
				b3Warning("Overlapping object query failed");
				break;
			}
			case CMD_REQUEST_AABB_OVERLAP_COMPLETED:
			{
				if (m_data->m_verboseOutput)
				{
					b3Printf("Overlapping object request completed");
				}

				int startOverlapIndex = serverCmd.m_sendOverlappingObjectsArgs.m_startingOverlappingObjectIndex;
				int numOverlapCopied = serverCmd.m_sendOverlappingObjectsArgs.m_numOverlappingObjectsCopied;
				m_data->m_cachedOverlappingObjects.resize(startOverlapIndex + numOverlapCopied);
				b3OverlappingObject* objects = (b3OverlappingObject*)m_data->m_testBlock1->m_bulletStreamDataServerToClientRefactor;

				for (int i = 0; i < numOverlapCopied; i++)
				{
					m_data->m_cachedOverlappingObjects[startOverlapIndex + i] = objects[i];
				}

				break;
			}
            case CMD_CONTACT_POINT_INFORMATION_COMPLETED:
                {
                    if (m_data->m_verboseOutput) 
                    {
                        b3Printf("Contact Point Information Request OK\n");
                    }
					int startContactIndex = serverCmd.m_sendContactPointArgs.m_startingContactPointIndex;
					int numContactsCopied = serverCmd.m_sendContactPointArgs.m_numContactPointsCopied;

					m_data->m_cachedContactPoints.resize(startContactIndex+numContactsCopied);
                    
					b3ContactPointData* contactData = (b3ContactPointData*)m_data->m_testBlock1->m_bulletStreamDataServerToClientRefactor;

					for (int i=0;i<numContactsCopied;i++)
					{
						m_data->m_cachedContactPoints[startContactIndex+i] = contactData[i];
					}

                    break;
                }
            case CMD_CONTACT_POINT_INFORMATION_FAILED:
                {
                    b3Warning("Contact Point Information Request failed");
                    break;
                }

			case CMD_SAVE_WORLD_COMPLETED:
				break;
					
			case CMD_SAVE_WORLD_FAILED:
			{
				b3Warning("Saving world  failed");
				break;
			}
			case CMD_CALCULATE_INVERSE_KINEMATICS_COMPLETED:
			{
                    break;
                }
            case CMD_CALCULATE_INVERSE_KINEMATICS_FAILED:
                {
                    b3Warning("Calculate Inverse Kinematics Request failed");
                    break;
                }
			case CMD_VISUAL_SHAPE_INFO_COMPLETED:
			{
				if (m_data->m_verboseOutput)
				{
					b3Printf("Visual Shape Information Request OK\n");
				}
				int startVisualShapeIndex = serverCmd.m_sendVisualShapeArgs.m_startingVisualShapeIndex;
				int numVisualShapesCopied = serverCmd.m_sendVisualShapeArgs.m_numVisualShapesCopied;
				m_data->m_cachedVisualShapes.resize(startVisualShapeIndex + numVisualShapesCopied);
				b3VisualShapeData* shapeData = (b3VisualShapeData*)m_data->m_testBlock1->m_bulletStreamDataServerToClientRefactor;
				for (int i = 0; i < numVisualShapesCopied; i++)
				{
					m_data->m_cachedVisualShapes[startVisualShapeIndex + i] = shapeData[i];
				}

				break;
			}
			case CMD_VISUAL_SHAPE_INFO_FAILED:
			{
				b3Warning("Visual Shape Info Request failed");
				break;
			}
            case CMD_VISUAL_SHAPE_UPDATE_COMPLETED:
            {
                break;
            }
            case CMD_VISUAL_SHAPE_UPDATE_FAILED:
            {
                b3Warning("Visual Shape Update failed");
                break;
            }
            case CMD_LOAD_TEXTURE_COMPLETED:
            {
                break;
            }
            case CMD_LOAD_TEXTURE_FAILED:
            {
                b3Warning("Load texture failed");
                break;
            }
			case CMD_BULLET_LOADING_COMPLETED:
			{
				break;
			}
			case CMD_BULLET_LOADING_FAILED:
			{
				b3Warning("Load .bullet failed");
				break;
			}
			case CMD_BULLET_SAVING_FAILED:
			{
				b3Warning("Save .bullet failed");
				break;
			}
			case CMD_MJCF_LOADING_FAILED:
			{
				b3Warning("Load .mjcf failed");
				break;
			}
			case CMD_USER_DEBUG_DRAW_COMPLETED:
			{
				break;
			}
			case CMD_USER_DEBUG_DRAW_FAILED:
			{
				b3Warning("User debug draw failed");
				break;
			}
			case CMD_USER_CONSTRAINT_COMPLETED:
			{
				break;
			}
			case CMD_USER_CONSTRAINT_FAILED:
			{
				b3Warning("createConstraint failed");
				break;
			}
            default: {
                b3Error("Unknown server status %d\n", serverCmd.m_type);
                btAssert(0);
            }
        };

        m_data->m_testBlock1->m_numProcessedServerCommands++;
        // we don't have more than 1 command outstanding (in total, either server or client)
        btAssert(m_data->m_testBlock1->m_numProcessedServerCommands ==
                 m_data->m_testBlock1->m_numServerCommands);

        if (m_data->m_testBlock1->m_numServerCommands ==
            m_data->m_testBlock1->m_numProcessedServerCommands) {
            m_data->m_waitingForServer = false;
        } else {
            m_data->m_waitingForServer = true;
        }

        if (serverCmd.m_type == CMD_SDF_LOADING_COMPLETED)
        {
            int numBodies = serverCmd.m_sdfLoadedArgs.m_numBodies;
            if (numBodies>0)
            {
                m_data->m_tempBackupServerStatus = m_data->m_lastServerStatus;
                
                for (int i=0;i<numBodies;i++)
                {
                    m_data->m_bodyIdsRequestInfo.push_back(serverCmd.m_sdfLoadedArgs.m_bodyUniqueIds[i]);
                }
                
                int bodyId = m_data->m_bodyIdsRequestInfo[m_data->m_bodyIdsRequestInfo.size()-1];
                m_data->m_bodyIdsRequestInfo.pop_back();
                
                SharedMemoryCommand& command = m_data->m_testBlock1->m_clientCommands[0];
                //now transfer the information of the individual objects etc.
                command.m_type = CMD_REQUEST_BODY_INFO;
                command.m_sdfRequestInfoArgs.m_bodyUniqueId = bodyId;
                submitClientCommand(command);
                return 0;    
            }
        }
        
        if (serverCmd.m_type == CMD_BODY_INFO_COMPLETED)
        {
            //are there any bodies left to be processed?
            if (m_data->m_bodyIdsRequestInfo.size())
            {
                int bodyId = m_data->m_bodyIdsRequestInfo[m_data->m_bodyIdsRequestInfo.size()-1];
                m_data->m_bodyIdsRequestInfo.pop_back();
                
                SharedMemoryCommand& command = m_data->m_testBlock1->m_clientCommands[0];
                //now transfer the information of the individual objects etc.
                command.m_type = CMD_REQUEST_BODY_INFO;
                command.m_sdfRequestInfoArgs.m_bodyUniqueId = bodyId;
                submitClientCommand(command);
                return 0;
            } else
            {
                m_data->m_lastServerStatus = m_data->m_tempBackupServerStatus;
            }
        }

		if (serverCmd.m_type == CMD_REQUEST_AABB_OVERLAP_COMPLETED)
		{
			SharedMemoryCommand& command = m_data->m_testBlock1->m_clientCommands[0];
			if (serverCmd.m_sendOverlappingObjectsArgs.m_numRemainingOverlappingObjects > 0 && serverCmd.m_sendOverlappingObjectsArgs.m_numOverlappingObjectsCopied)
			{
				command.m_type = CMD_REQUEST_AABB_OVERLAP;
				command.m_requestOverlappingObjectsArgs.m_startingOverlappingObjectIndex = serverCmd.m_sendOverlappingObjectsArgs.m_startingOverlappingObjectIndex + serverCmd.m_sendOverlappingObjectsArgs.m_numOverlappingObjectsCopied;
				submitClientCommand(command);
				return 0;
			}
		}
        
        if (serverCmd.m_type == CMD_CONTACT_POINT_INFORMATION_COMPLETED)
        {
            SharedMemoryCommand& command = m_data->m_testBlock1->m_clientCommands[0];
			if (serverCmd.m_sendContactPointArgs.m_numRemainingContactPoints>0 && serverCmd.m_sendContactPointArgs.m_numContactPointsCopied)
			{
				command.m_type = CMD_REQUEST_CONTACT_POINT_INFORMATION;
				command.m_requestContactPointArguments.m_startingContactPointIndex = serverCmd.m_sendContactPointArgs.m_startingContactPointIndex+serverCmd.m_sendContactPointArgs.m_numContactPointsCopied;
				command.m_requestContactPointArguments.m_objectAIndexFilter = -1;
				command.m_requestContactPointArguments.m_objectBIndexFilter = -1;
				submitClientCommand(command);
				return 0;
			}
        }
        
		if (serverCmd.m_type == CMD_VISUAL_SHAPE_INFO_COMPLETED)
		{
			SharedMemoryCommand& command = m_data->m_testBlock1->m_clientCommands[0];
			if (serverCmd.m_sendVisualShapeArgs.m_numRemainingVisualShapes >0 && serverCmd.m_sendVisualShapeArgs.m_numVisualShapesCopied)
			{
				command.m_type = CMD_REQUEST_VISUAL_SHAPE_INFO;
				command.m_requestVisualShapeDataArguments.m_startingVisualShapeIndex = serverCmd.m_sendVisualShapeArgs.m_startingVisualShapeIndex + serverCmd.m_sendVisualShapeArgs.m_numVisualShapesCopied;
				command.m_requestVisualShapeDataArguments.m_bodyUniqueId = serverCmd.m_sendVisualShapeArgs.m_bodyUniqueId;
				submitClientCommand(command);
				return 0;
			}
		}

		

		if (serverCmd.m_type == CMD_CAMERA_IMAGE_COMPLETED)
		{
			SharedMemoryCommand& command = m_data->m_testBlock1->m_clientCommands[0];

			if (serverCmd.m_sendPixelDataArguments.m_numRemainingPixels > 0 && serverCmd.m_sendPixelDataArguments.m_numPixelsCopied)
			{
				

				// continue requesting remaining pixels
				command.m_type = CMD_REQUEST_CAMERA_IMAGE_DATA;
				command.m_requestPixelDataArguments.m_startPixelIndex = 
					serverCmd.m_sendPixelDataArguments.m_startingPixelIndex + 
					serverCmd.m_sendPixelDataArguments.m_numPixelsCopied;
				submitClientCommand(command);
				return 0;
			} else
			{
				m_data->m_cachedCameraPixelsWidth = serverCmd.m_sendPixelDataArguments.m_imageWidth;
				m_data->m_cachedCameraPixelsHeight = serverCmd.m_sendPixelDataArguments.m_imageHeight;
			}	


        }

        if ((serverCmd.m_type == CMD_DEBUG_LINES_COMPLETED) &&
            (serverCmd.m_sendDebugLinesArgs.m_numRemainingDebugLines > 0)) {
            SharedMemoryCommand& command = m_data->m_testBlock1->m_clientCommands[0];

            // continue requesting debug lines for drawing
            command.m_type = CMD_REQUEST_DEBUG_LINES;
            command.m_requestDebugLinesArguments.m_startingLineIndex =
                serverCmd.m_sendDebugLinesArgs.m_numDebugLines +
                serverCmd.m_sendDebugLinesArgs.m_startingLineIndex;
            submitClientCommand(command);
            return 0;
        }

        return &m_data->m_lastServerStatus;

    } else {
        if (m_data->m_verboseOutput) {
            b3Printf("m_numServerStatus  = %d, processed = %d\n",
                     m_data->m_testBlock1->m_numServerCommands,
                     m_data->m_testBlock1->m_numProcessedServerCommands);
        }
    }
    return 0;
}

bool PhysicsClientSharedMemory::canSubmitCommand() const {
    return (m_data->m_isConnected && !m_data->m_waitingForServer);
}

struct SharedMemoryCommand* PhysicsClientSharedMemory::getAvailableSharedMemoryCommand() {
    static int sequence = 0;
    m_data->m_testBlock1->m_clientCommands[0].m_sequenceNumber = sequence++;
    return &m_data->m_testBlock1->m_clientCommands[0];
}

bool PhysicsClientSharedMemory::submitClientCommand(const SharedMemoryCommand& command) {
    /// at the moment we allow a maximum of 1 outstanding command, so we check for this
    // once the server processed the command and returns a status, we clear the flag
    // "m_data->m_waitingForServer" and allow submitting the next command

    if (!m_data->m_waitingForServer) {
        if (&m_data->m_testBlock1->m_clientCommands[0] != &command) {
            m_data->m_testBlock1->m_clientCommands[0] = command;
        }
        m_data->m_testBlock1->m_numClientCommands++;
        m_data->m_waitingForServer = true;
        return true;
    }
    return false;
}

void PhysicsClientSharedMemory::uploadBulletFileToSharedMemory(const char* data, int len) {
    btAssert(len < SHARED_MEMORY_MAX_STREAM_CHUNK_SIZE);
    if (len >= SHARED_MEMORY_MAX_STREAM_CHUNK_SIZE) {
        b3Warning("uploadBulletFileToSharedMemory %d exceeds max size %d\n", len,
                  SHARED_MEMORY_MAX_STREAM_CHUNK_SIZE);
    } else {
        for (int i = 0; i < len; i++) {
            m_data->m_testBlock1->m_bulletStreamDataClientToServer[i] = data[i];
        }
    }
}

void PhysicsClientSharedMemory::getCachedCameraImage(struct b3CameraImageData* cameraData)
{
	cameraData->m_pixelWidth = m_data->m_cachedCameraPixelsWidth;
	cameraData->m_pixelHeight = m_data->m_cachedCameraPixelsHeight;
	cameraData->m_depthValues = m_data->m_cachedCameraDepthBuffer.size() ? &m_data->m_cachedCameraDepthBuffer[0] : 0;
	cameraData->m_rgbColorData = m_data->m_cachedCameraPixelsRGBA.size() ? &m_data->m_cachedCameraPixelsRGBA[0] : 0;
	cameraData->m_segmentationMaskValues = m_data->m_cachedSegmentationMaskBuffer.size()?&m_data->m_cachedSegmentationMaskBuffer[0] : 0;
}

void PhysicsClientSharedMemory::getCachedContactPointInformation(struct b3ContactInformation* contactPointData)
{
	contactPointData->m_numContactPoints = m_data->m_cachedContactPoints.size();
	contactPointData->m_contactPointData = contactPointData->m_numContactPoints? &m_data->m_cachedContactPoints[0] : 0;

}

void PhysicsClientSharedMemory::getCachedOverlappingObjects(struct b3AABBOverlapData* overlappingObjects)
{
	overlappingObjects->m_numOverlappingObjects = m_data->m_cachedOverlappingObjects.size();
	overlappingObjects->m_overlappingObjects = m_data->m_cachedOverlappingObjects.size() ?
		&m_data->m_cachedOverlappingObjects[0] : 0;
}


void PhysicsClientSharedMemory::getCachedVisualShapeInformation(struct b3VisualShapeInformation* visualShapesInfo)
{
	visualShapesInfo->m_numVisualShapes = m_data->m_cachedVisualShapes.size();
	visualShapesInfo->m_visualShapeData = visualShapesInfo->m_numVisualShapes ? &m_data->m_cachedVisualShapes[0] : 0;
}


const float* PhysicsClientSharedMemory::getDebugLinesFrom() const {
    if (m_data->m_debugLinesFrom.size()) {
        return &m_data->m_debugLinesFrom[0].m_x;
    }
    return 0;
}
const float* PhysicsClientSharedMemory::getDebugLinesTo() const {
    if (m_data->m_debugLinesTo.size()) {
        return &m_data->m_debugLinesTo[0].m_x;
    }
    return 0;
}
const float* PhysicsClientSharedMemory::getDebugLinesColor() const {
    if (m_data->m_debugLinesColor.size()) {
        return &m_data->m_debugLinesColor[0].m_x;
    }
    return 0;
}
int PhysicsClientSharedMemory::getNumDebugLines() const { return m_data->m_debugLinesFrom.size(); }
