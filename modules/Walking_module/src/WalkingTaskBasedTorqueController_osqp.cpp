/**
 * @file WalkingTaskBasedTorqueController_osqp.cpp
 * @authors Giulio Romualdi <giulio.romualdi@iit.it>
 * @copyright 2018 iCub Facility - Istituto Italiano di Tecnologia
 *            Released under the terms of the LGPLv2.1 or later, see LGPL.TXT
 * @date 2018
 */

#include <iDynTree/yarp/YARPConfigurationsLoader.h>
#include <iDynTree/Core/Twist.h>
#include <iDynTree/Core/Wrench.h>
#include <iDynTree/Core/EigenHelpers.h>
#include <iDynTree/Core/EigenSparseHelpers.h>

#include <WalkingTaskBasedTorqueController_osqp.hpp>
#include <Utils.hpp>

typedef Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> MatrixXd;

bool WalkingTaskBasedTorqueController_osqp::instantiateCoMConstraint(const yarp::os::Searchable& config)
{
    if(config.isNull())
    {
        yError() << "[instantiateCoMConstraint] Empty configuration file.";
        return false;
    }

    double kp;
    if(!YarpHelper::getNumberFromSearchable(config, "kp", kp))
    {
        yError() << "[instantiateCoMConstraint] Unable to get proportional gain";
        return false;
    }

    double kd;
    if(!YarpHelper::getNumberFromSearchable(config, "kd", kd))
    {
        yError() << "[instantiateCoMConstraint] Unable to get derivative gain";
        return false;
    }

    // resize com quantities
    m_comJacobian.resize(3, m_actuatedDOFs + 6);
    m_comBiasAcceleration.resize(3);

    // memory allocation
    std::shared_ptr<PositionConstraint> ptr;
    ptr = std::make_shared<PositionConstraint>(m_actuatedDOFs + 6);
    ptr->setSubMatricesStartingPosition(m_numberOfConstraints, 0);

    ptr->positionController()->setGains(kp, kd);
    ptr->setRoboticJacobian(m_comJacobian);
    ptr->setBiasAcceleration(m_comBiasAcceleration);

    m_constraints.insert(std::make_pair("com", ptr));
    m_numberOfConstraints += ptr->getNumberOfConstraints();

    return true;
}

bool WalkingTaskBasedTorqueController_osqp::instantiateFeetConstraint(const yarp::os::Searchable& config)
{
    if(config.isNull())
    {
        yError() << "[instantiateFeetConstraint] Empty configuration file.";
        return false;
    }

    double kp;
    if(!YarpHelper::getNumberFromSearchable(config, "kp", kp))
    {
        yError() << "[instantiateFeetConstraint] Unable to get proportional gain";
        return false;
    }

    double kd;
    if(!YarpHelper::getNumberFromSearchable(config, "kd", kd))
    {
        yError() << "[instantiateFeetConstraint] Unable to get derivative gain";
        return false;
    }

    double c0;
    if(!YarpHelper::getNumberFromSearchable(config, "c0", c0))
    {
        yError() << "[instantiateFeetConstraint] Unable to get c0";
        return false;
    }

    double c1;
    if(!YarpHelper::getNumberFromSearchable(config, "c1", c1))
    {
        yError() << "[instantiateFeetConstraint] Unable to get c1";
        return false;
    }

    double c2;
    if(!YarpHelper::getNumberFromSearchable(config, "c2", c2))
    {
        yError() << "[instantiateFeetConstraint] Unable to get c2";
        return false;
    }

    std::shared_ptr<CartesianConstraint> ptr;

    // left foot
    // resize quantities
    m_leftFootJacobian.resize(6, m_actuatedDOFs + 6);
    m_leftFootBiasAcceleration.resize(6);

    ptr = std::make_shared<CartesianConstraint>(m_actuatedDOFs + 6);
    ptr->setSubMatricesStartingPosition(m_numberOfConstraints, 0);
    ptr->positionController()->setGains(kp, kd);
    ptr->orientationController()->setGains(c0, c1, c2);
    ptr->setRoboticJacobian(m_leftFootJacobian);
    ptr->setBiasAcceleration(m_leftFootBiasAcceleration);
    m_constraints.insert(std::make_pair("left_foot", ptr));
    m_numberOfConstraints += ptr->getNumberOfConstraints();

    // right foot
    // resize quantities
    m_rightFootJacobian.resize(6, m_actuatedDOFs + 6);
    m_rightFootBiasAcceleration.resize(6);
    ptr = std::make_shared<CartesianConstraint>(m_actuatedDOFs + 6);
    ptr->setSubMatricesStartingPosition(m_numberOfConstraints, 0);
    ptr->positionController()->setGains(kp, kd);
    ptr->orientationController()->setGains(c0, c1, c2);
    ptr->setRoboticJacobian(m_rightFootJacobian);
    ptr->setBiasAcceleration(m_rightFootBiasAcceleration);
    m_constraints.insert(std::make_pair("right_foot", ptr));
    m_numberOfConstraints += ptr->getNumberOfConstraints();

    return true;
}

void WalkingTaskBasedTorqueController_osqp::instantiateZMPConstraint()
{
    // std::shared_ptr<ZMPConstraint> ptr;
    // ptr = std::make_shared<ZMPConstraint>();
    // ptr->setSubMatricesStartingPosition(m_numberOfConstraints, m_actuatedDOFs);
    // ptr->setLeftFootToWorldTransform(m_leftFootToWorldTransform);
    // ptr->setRightFootToWorldTransform(m_rightFootToWorldTransform);

    // m_constraints.insert(std::make_pair("zmp", ptr));

    // m_numberOfConstraints += ptr->getNumberOfConstraints();
}

bool WalkingTaskBasedTorqueController_osqp::instantiateContactForcesConstraint(const yarp::os::Searchable& config)
{
    if(config.isNull())
    {
        yError() << "[instantiateFeetConstraint] Empty configuration file.";
        return false;
    }

    double staticFrictionCoefficient;
    if(!YarpHelper::getNumberFromSearchable(config, "staticFrictionCoefficient",
                                            staticFrictionCoefficient))
    {
        yError() << "[initialize] Unable to get the number from searchable.";
        return false;
    }

    int numberOfPoints;
    if(!YarpHelper::getNumberFromSearchable(config, "numberOfPoints", numberOfPoints))
    {
        yError() << "[initialize] Unable to get the number from searchable.";
        return false;
    }

    double torsionalFrictionCoefficient;
    if(!YarpHelper::getNumberFromSearchable(config, "torsionalFrictionCoefficient",
                                            torsionalFrictionCoefficient))
    {
        yError() << "[initialize] Unable to get the number from searchable.";
        return false;
    }

    // feet dimensions
    yarp::os::Value feetDimensions = config.find("foot_size");
    if(feetDimensions.isNull() || !feetDimensions.isList())
    {
        yError() << "Please set the foot_size in the configuration file.";
        return false;
    }

    yarp::os::Bottle *feetDimensionsPointer = feetDimensions.asList();
    if(!feetDimensionsPointer || feetDimensionsPointer->size() != 2)
    {
        yError() << "Error while reading the feet dimensions. Wrong number of elements.";
        return false;
    }

    yarp::os::Value& xLimits = feetDimensionsPointer->get(0);
    if(xLimits.isNull() || !xLimits.isList())
    {
        yError() << "Error while reading the X limits.";
        return false;
    }

    yarp::os::Bottle *xLimitsPtr = xLimits.asList();
    if(!xLimitsPtr || xLimitsPtr->size() != 2)
    {
        yError() << "Error while reading the X limits. Wrong dimensions.";
        return false;
    }

    iDynTree::Vector2 footLimitX;
    footLimitX(0) = xLimitsPtr->get(0).asDouble();
    footLimitX(1) = xLimitsPtr->get(1).asDouble();

    yarp::os::Value& yLimits = feetDimensionsPointer->get(1);
    if(yLimits.isNull() || !yLimits.isList())
    {
        yError() << "Error while reading the Y limits.";
        return false;
    }

    yarp::os::Bottle *yLimitsPtr = yLimits.asList();
    if(!yLimitsPtr || yLimitsPtr->size() != 2)
    {
        yError() << "Error while reading the Y limits. Wrong dimensions.";
        return false;
    }

    iDynTree::Vector2 footLimitY;
    footLimitY(0) = yLimitsPtr->get(0).asDouble();
    footLimitY(1) = yLimitsPtr->get(1).asDouble();

    double minimalNormalForce;
    if(!YarpHelper::getNumberFromSearchable(config, "minimalNormalForce", minimalNormalForce))
    {
        yError() << "[initialize] Unable to get the number from searchable.";
        return false;
    }

    std::shared_ptr<ForceConstraint> ptr;

    // left foot
    ptr = std::make_shared<ForceConstraint>(numberOfPoints);
    ptr->setSubMatricesStartingPosition(m_numberOfConstraints, 2 * m_actuatedDOFs + 6);

    ptr->setStaticFrictionCoefficient(staticFrictionCoefficient);
    ptr->setTorsionalFrictionCoefficient(torsionalFrictionCoefficient);
    ptr->setMinimalNormalForce(minimalNormalForce);
    ptr->setFootSize(footLimitX, footLimitY);
    ptr->setFootToWorldTransform(m_leftFootToWorldTransform);

    m_constraints.insert(std::make_pair("left_force", ptr));
    m_numberOfConstraints += ptr->getNumberOfConstraints();

    // right foot
    ptr = std::make_shared<ForceConstraint>(numberOfPoints);
    ptr->setSubMatricesStartingPosition(m_numberOfConstraints, 2 * m_actuatedDOFs + 6 + 6);

    ptr->setStaticFrictionCoefficient(staticFrictionCoefficient);
    ptr->setTorsionalFrictionCoefficient(torsionalFrictionCoefficient);
    ptr->setMinimalNormalForce(minimalNormalForce);
    ptr->setFootSize(footLimitX, footLimitY);
    ptr->setFootToWorldTransform(m_rightFootToWorldTransform);

    m_constraints.insert(std::make_pair("right_force", ptr));
    m_numberOfConstraints += ptr->getNumberOfConstraints();

    return true;
}

void WalkingTaskBasedTorqueController_osqp::instantiateSystemDynamicsConstraint()
{
    std::shared_ptr<SystemDynamicConstraint> ptr;
    ptr = std::make_shared<SystemDynamicConstraint>(m_actuatedDOFs + 6, m_numberOfVariables,
                                                    m_actuatedDOFs);
    ptr->setSubMatricesStartingPosition(m_numberOfConstraints, 0);
    ptr->setLeftFootJacobian(m_leftFootJacobian);
    ptr->setRightFootJacobian(m_rightFootJacobian);
    ptr->setMassMatrix(m_massMatrix);
    ptr->setGeneralizedBiasForces(m_generalizedBiasForces);

    m_constraints.insert(std::make_pair("system_dynamics", ptr));
    m_numberOfConstraints += ptr->getNumberOfConstraints();
}

bool WalkingTaskBasedTorqueController_osqp::instantiateNeckSoftConstraint(const yarp::os::Searchable& config)
{
    yarp::os::Value tempValue;
    m_neckOrientationController = std::make_unique<RotationalPID>();
    if(config.isNull())
    {
        yError() << "[instantiateNeckSoftConstraint] Empty configuration neck soft constraint.";
        return false;
    }

    // get the neck weight
    tempValue = config.find("neckWeight");
    m_neckOrientationWeight.resize(3);
    if(!YarpHelper::yarpListToiDynTreeVectorDynSize(tempValue, m_neckOrientationWeight))
    {
        yError() << "[instantiateNeckSoftConstraint] Initialization failed while reading neckWeight.";
        return false;
    }

    double c0, c1, c2;
    if(!YarpHelper::getNumberFromSearchable(config, "c0", c0))
        return false;

    if(!YarpHelper::getNumberFromSearchable(config, "c1", c1))
        return false;

    if(!YarpHelper::getNumberFromSearchable(config, "c2", c2))
        return false;

    m_neckOrientationController->setGains(c0, c1, c2);

    if(!iDynTree::parseRotationMatrix(config, "additional_rotation", m_additionalRotation))
    {
        yError() << "[instantiateNeckSoftConstraint] Unable to set the additional rotation.";
        return false;
    }

    m_neckJacobian.resize(3, m_actuatedDOFs + 6);

    // resize useful matrix
    m_neckHessian.resize(m_numberOfVariables, m_numberOfVariables);
    m_neckHessianSubMatrix.resize(m_actuatedDOFs + 6, m_actuatedDOFs + 6);

    // notice only the neck orientation is used
    m_neckGradient.resize(m_numberOfVariables, 3);
    m_neckGradientSubMatrix.resize(m_actuatedDOFs + 6, 3);

    return true;
}

bool WalkingTaskBasedTorqueController_osqp::instantiateRegularizationTaskConstraint(const yarp::os::Searchable& config)
{
    yarp::os::Value tempValue;

    if(config.isNull())
    {
        yError() << "[instantiateRegularizationTaskConstraint] Empty configuration joint task constraint.";
        return false;
    }

    m_desiredJointPosition.resize(m_actuatedDOFs);
    m_desiredJointVelocity.resize(m_actuatedDOFs);
    m_desiredJointAcceleration.resize(m_actuatedDOFs);
    m_desiredJointVelocity.zero();
    m_desiredJointAcceleration.zero();
    tempValue = config.find("jointRegularization");
    if(!YarpHelper::yarpListToiDynTreeVectorDynSize(tempValue, m_desiredJointPosition))
    {
        yError() << "[initialize] Unable to convert a YARP list to an iDynTree::VectorDynSize, "
                 << "joint regularization";
        return false;
    }

    iDynTree::toEigen(m_desiredJointPosition) = iDynTree::toEigen(m_desiredJointPosition) *
        iDynTree::deg2rad(1);

    // set the matrix related to the joint regularization
    tempValue = config.find("jointRegularizationWeights");
    iDynTree::VectorDynSize jointRegularizationWeights(m_actuatedDOFs);
    if(!YarpHelper::yarpListToiDynTreeVectorDynSize(tempValue, jointRegularizationWeights))
    {
        yError() << "Initialization failed while reading jointRegularizationWeights vector.";
        return false;
    }

    //  m_jointRegularizationHessian = H' \lamda H
    m_jointRegularizationHessian.resize(m_numberOfVariables, m_numberOfVariables);
    for(int i = 0; i < m_actuatedDOFs; i++)
        m_jointRegularizationHessian(i + 6, i + 6) = jointRegularizationWeights(i);


    // evaluate constant sub-matrix of the gradient matrix
    m_jointRegularizationGradient.resize(m_numberOfVariables, m_actuatedDOFs);
    for(int i = 0; i < m_actuatedDOFs; i++)
        m_jointRegularizationGradient(i + 6, i) = jointRegularizationWeights(i);

    // set the matrix related to the joint regularization
    tempValue = config.find("jointRegularizationProportionalGains");
    m_jointRegularizationProportionalGains.resize(m_actuatedDOFs);
    if(!YarpHelper::yarpListToiDynTreeVectorDynSize(tempValue,
                                                    m_jointRegularizationProportionalGains))
    {
        yError() << "Initialization failed while reading jointRegularizationProportionalGains vector.";
        return false;
    }

    tempValue = config.find("jointRegularizationDerivativeGains");
    m_jointRegularizationDerivativeGains.resize(m_actuatedDOFs);
    if(!YarpHelper::yarpListToiDynTreeVectorDynSize(tempValue, m_jointRegularizationDerivativeGains))
    {
        yError() << "Initialization failed while reading jointRegularizationDerivativeGains vector.";
        return false;
    }

    // todo move in a separate class
    m_desiredJointAccelerationController.resize(m_actuatedDOFs);

    return true;
}

bool WalkingTaskBasedTorqueController_osqp::instantiateInputRegularizationConstraint(const yarp::os::Searchable& config)
{
    yarp::os::Value tempValue;

    if(config.isNull())
    {
        yError() << "[instantiateRegularizationTaskConstraint] Empty configuration input constraint.";
        return false;
    }

    tempValue = config.find("inputRegularizationWeights");
    iDynTree::VectorDynSize inputRegularizationWeights(m_actuatedDOFs + 12);
    if(!YarpHelper::yarpListToiDynTreeVectorDynSize(tempValue, inputRegularizationWeights))
    {
        yError() << "Initialization failed while reading inputRegularizationWeights vector.";
        return false;
    }

    //  m_inputRegularizationHessian = H' \lamda H
    m_inputRegularizationHessian.resize(m_numberOfVariables, m_numberOfVariables);
    for(int i = 0; i < m_actuatedDOFs + 12; i++)
        m_inputRegularizationHessian(i + m_actuatedDOFs + 6,
                                     i + m_actuatedDOFs + 6) = inputRegularizationWeights(i);

    m_inputRegularizationGradient.resize(m_numberOfVariables, m_actuatedDOFs + 12);
    for(int i = 0; i < m_actuatedDOFs + 12; i++)
        m_inputRegularizationGradient(i + m_actuatedDOFs + 6, i) = inputRegularizationWeights(i);

    return true;
}

bool WalkingTaskBasedTorqueController_osqp::initialize(const yarp::os::Searchable& config,
                                                       const int& actuatedDOFs,
                                                       const iDynTree::VectorDynSize& minJointTorque,
                                                       const iDynTree::VectorDynSize& maxJointTorque)
{

    m_profiler = std::make_unique<TimeProfiler>();
    m_profiler->setPeriod(round(0.1 / 0.01));

    m_profiler->addTimer("hessian");
    m_profiler->addTimer("gradient");
    m_profiler->addTimer("A");
    m_profiler->addTimer("add A");
    m_profiler->addTimer("bounds");
    m_profiler->addTimer("solve");

    m_actuatedDOFs = actuatedDOFs;
    // the optimization variable is given by
    // 1. base + joint acceleration
    // 2. joint torque
    // 2. left and right contact wrench
    m_numberOfVariables = 6 + m_actuatedDOFs + m_actuatedDOFs + 6 + 6;
    m_numberOfConstraints = 0;

    // resize matrices (generic)
    m_inputMatrix.resize(m_actuatedDOFs + 6, m_numberOfVariables);
    m_massMatrix.resize(m_actuatedDOFs + 6, m_actuatedDOFs + 6);
    m_generalizedBiasForces.resize(m_actuatedDOFs + 6);

    // results
    m_solution.resize(m_numberOfVariables);

    // check if the config is empty
    if(config.isNull())
    {
        yError() << "[initialize] Empty configuration for Task based torque solver.";
        return false;
    }

    // instantiate constraints
    yarp::os::Bottle& comConstraintOptions = config.findGroup("COM");
    if(!instantiateCoMConstraint(comConstraintOptions))
    {
        yError() << "[initialize] Unable to get the instantiate the CoM constraint.";
        return false;
    }

    yarp::os::Bottle& feetConstraintOptions = config.findGroup("FEET");
    if(!instantiateFeetConstraint(feetConstraintOptions))
    {
        yError() << "[initialize] Unable to get the instantiate the feet constraints.";
        return false;
    }

    instantiateZMPConstraint();

    yarp::os::Bottle& contactForcesOption = config.findGroup("CONTACT_FORCES");
    if(!instantiateContactForcesConstraint(contactForcesOption))
    {
        yError() << "[initialize] Unable to get the instantiate the feet constraints.";
        return false;
    }

    // instantiate cost function
    yarp::os::Bottle& neckOrientationOption = config.findGroup("NECK_ORIENTATION");
    if(!instantiateNeckSoftConstraint(neckOrientationOption))
    {
        yError() << "[initialize] Unable to get the instantiate the neck constraint.";
        return false;
    }

    yarp::os::Bottle& regularizationTaskOption = config.findGroup("REGULARIZATION_TASK");
    if(!instantiateRegularizationTaskConstraint(regularizationTaskOption))
    {
        yError() << "[initialize] Unable to get the instantiate the regularization constraint.";
        return false;
    }

    yarp::os::Bottle& regularizationInputOption = config.findGroup("REGULARIZATION_INPUT");
    if(!instantiateInputRegularizationConstraint(regularizationInputOption))
    {
        yError() << "[initialize] Unable to get the instantiate the regularization input constraint.";
        return false;
    }

    instantiateSystemDynamicsConstraint();

    // add torque constraints
    m_numberOfConstraints += m_actuatedDOFs;

    // resize
    // sparse matrix
    m_hessianEigen.resize(m_numberOfVariables, m_numberOfVariables);
    m_constraintMatrix.resize(m_numberOfConstraints, m_numberOfVariables);
    m_constraintMatrixEigen.resize(m_numberOfConstraints, m_numberOfVariables);

    // dense vectors
    m_gradient = Eigen::VectorXd::Zero(m_numberOfVariables);
    m_lowerBound = Eigen::VectorXd::Zero(m_numberOfConstraints);
    m_upperBound = Eigen::VectorXd::Zero(m_numberOfConstraints);

    // joint torque limits
    for(int i = 0; i<m_actuatedDOFs; i++)
    {
        // todo minJointTorque and maxJointTorque
        m_upperBound(m_numberOfConstraints - m_actuatedDOFs + i) = 60;
        m_lowerBound(m_numberOfConstraints - m_actuatedDOFs + i) = -60;
        m_constraintMatrixEigen.insert(m_numberOfConstraints - m_actuatedDOFs + i,
                                       m_actuatedDOFs + 6 + i) = 1;
    }

    m_constraintMatrixTriplet.addDiagonalMatrix(m_numberOfConstraints - m_actuatedDOFs,
                                                m_actuatedDOFs + 6, 1, m_actuatedDOFs);

    // initialize the optimization problem
    m_optimizer = std::make_unique<OsqpEigen::Solver>();
    m_optimizer->data()->setNumberOfVariables(m_numberOfVariables);
    m_optimizer->data()->setNumberOfConstraints(m_numberOfConstraints);

    m_optimizer->settings()->setVerbosity(false);
    m_optimizer->settings()->setLinearSystemSolver(0);

    m_isSolutionEvaluated = false;

    // print some usefull information
    yInfo() << "Total number of constraints " << m_numberOfConstraints;
    for(const auto& constraint: m_constraints)
        yInfo() << constraint.first << ": " << constraint.second->getNumberOfConstraints();

    return true;
}

bool WalkingTaskBasedTorqueController_osqp::setInitialValues(const iDynTree::VectorDynSize& jointTorque,
                                                             const iDynTree::Wrench& leftWrench,
                                                             const iDynTree::Wrench& rightWrench)
{
    if(jointTorque.size() != m_actuatedDOFs)
    {
        yError() << "[setInitialValues] The size of the jointPosition vector is not coherent"
                 << " with the number of the actuated Joint";
        return false;
    }

    m_solution.block(m_actuatedDOFs + 6, 0, m_actuatedDOFs, 1) = iDynTree::toEigen(jointTorque);
    m_solution.block(m_actuatedDOFs + 6 + m_actuatedDOFs, 0, 6, 1) = iDynTree::toEigen(leftWrench);
    m_solution.block(m_actuatedDOFs + 6 + m_actuatedDOFs + 6, 0, 6, 1) = iDynTree::toEigen(rightWrench);
    return true;
}

bool WalkingTaskBasedTorqueController_osqp::setMassMatrix(const iDynTree::MatrixDynSize& massMatrix)
{
    if((massMatrix.rows() != m_actuatedDOFs + 6)
       ||(massMatrix.rows() != m_actuatedDOFs + 6))
    {
        yError() << "[setMassMatrix] The size of the massMatrix is not coherent "
                 << "with the number of the actuated Joint plus six";
        return false;
    }

    m_massMatrix = massMatrix;

    return true;
}

bool WalkingTaskBasedTorqueController_osqp::setGeneralizedBiasForces(const iDynTree::VectorDynSize& generalizedBiasForces)
{
    if(generalizedBiasForces.size() != m_actuatedDOFs + 6)
    {
        yError() << "[setGeneralizedBiasForces] The size of generalizedBiasForces has to be "
                 << m_actuatedDOFs + 6;
        return false;
    }

    m_generalizedBiasForces = generalizedBiasForces;

    return true;
}

bool WalkingTaskBasedTorqueController_osqp::setDesiredJointTrajectory(const iDynTree::VectorDynSize& desiredJointPosition,
                                                                      const iDynTree::VectorDynSize& desiredJointVelocity,
                                                                      const iDynTree::VectorDynSize& desiredJointAcceleration)
{
    if(desiredJointPosition.size() != m_actuatedDOFs)
    {
        yError() << "[setDesiredJointTrajectory] The size of the jointPosition vector is not coherent"
                 << " with the number of the actuated Joint";
        return false;
    }

    if(desiredJointVelocity.size() != m_actuatedDOFs)
    {
        yError() << "[setDesiredJointTrajectory] The size of the jointVelocity vector is not coherent"
                 << " with the number of the actuated Joint";
        return false;
    }

    if(desiredJointAcceleration.size() != m_actuatedDOFs)
    {
        yError() << "[setDesiredJointTrajectory] The size of the jointVelocity vector is not coherent"
                 << " with the number of the actuated Joint";
        return false;
    }

    m_desiredJointPosition = desiredJointPosition;
    m_desiredJointVelocity = desiredJointVelocity;
    m_desiredJointAcceleration = desiredJointAcceleration;

    return true;
}

bool WalkingTaskBasedTorqueController_osqp::setInternalRobotState(const iDynTree::VectorDynSize& jointPosition,
                                                                  const iDynTree::VectorDynSize& jointVelocity)
{
    if(jointPosition.size() != m_actuatedDOFs)
    {
        yError() << "[setInternalRobotState] The size of the jointPosition vector is not coherent "
                 << "with the number of the actuated Joint";
        return false;
    }

    if(jointVelocity.size() != m_actuatedDOFs)
    {
        yError() << "[setInternalRobotState] The size of the jointVelocity vector is not coherent "
                 << "with the number of the actuated Joint";
        return false;
    }

    m_jointPosition = jointPosition;
    m_jointVelocity = jointVelocity;

    return true;
}

void WalkingTaskBasedTorqueController_osqp::setDesiredNeckTrajectory(const iDynTree::Rotation& desiredNeckOrientation,
                                                                     const iDynTree::Vector3& desiredNeckVelocity,
                                                                     const iDynTree::Vector3& desiredNeckAcceleration)
{
    // debug
    m_desiredNeckOrientation = desiredNeckOrientation * m_additionalRotation;
    // todo are you sure about these equations?
    m_neckOrientationController->setDesiredTrajectory(desiredNeckAcceleration,
                                                      desiredNeckVelocity,
                                                      desiredNeckOrientation * m_additionalRotation);
}

void WalkingTaskBasedTorqueController_osqp::setNeckState(const iDynTree::Rotation& neckOrientation,
                                                         const iDynTree::Twist& neckVelocity)
{
    m_neckOrientationController->setFeedback(neckVelocity.getAngularVec3(), neckOrientation);
}

bool WalkingTaskBasedTorqueController_osqp::setNeckJacobian(const iDynTree::MatrixDynSize& neckJacobian)
{
    if(neckJacobian.rows() != 6)
    {
        yError() << "[setNeckMJacobian] the number of rows has to be equal to 6.";
        return false;
    }
    if(neckJacobian.cols() != m_actuatedDOFs + 6)
    {
        yError() << "[setNeckMJacobian] the number of rows has to be equal to" << m_actuatedDOFs + 6;
        return false;
    }

    iDynTree::toEigen(m_neckJacobian) = iDynTree::toEigen(neckJacobian).block(3, 0, 3,
                                                                              m_actuatedDOFs + 6);

    return true;
}

void WalkingTaskBasedTorqueController_osqp::setNeckBiasAcceleration(const iDynTree::Vector6 &neckBiasAcceleration)
{
    // get only the angular part
    iDynTree::toEigen(m_neckBiasAcceleration)
        = iDynTree::toEigen(neckBiasAcceleration).block(3, 0, 3, 1);
}

bool WalkingTaskBasedTorqueController_osqp::setDesiredFeetTrajectory(const iDynTree::Transform& leftFootToWorldTransform,
                                                                     const iDynTree::Transform& rightFootToWorldTransform,
                                                                     const iDynTree::Twist& leftFootTwist,
                                                                     const iDynTree::Twist& rightFootTwist,
                                                                     const iDynTree::Twist& leftFootAcceleration,
                                                                     const iDynTree::Twist& rightFootAcceleration)
{
    std::shared_ptr<CartesianConstraint> ptr;

    // save left foot trajectory
    auto constraint = m_constraints.find("left_foot");
    if(constraint == m_constraints.end())
    {
        yError() << "[setDesiredFeetTrajectory] unable to find the left foot constraint. "
                 << "Please call 'initialize()' method";
        return false;
    }
    iDynTree::Vector3 dummy;
    dummy.zero();
    ptr = std::static_pointer_cast<CartesianConstraint>(constraint->second);
    ptr->positionController()->setDesiredTrajectory(dummy,
                                                    leftFootTwist.getLinearVec3(),
                                                    leftFootToWorldTransform.getPosition());

    ptr->orientationController()->setDesiredTrajectory(dummy,
                                                       leftFootTwist.getAngularVec3(),
                                                       leftFootToWorldTransform.getRotation());

    // right foot
    constraint = m_constraints.find("right_foot");
    if(constraint == m_constraints.end())
    {
        yError() << "[setDesiredFeetTrajectory] unable to find the right foot constraint. "
                 << "Please call 'initialize()' method";
        return false;
    }
    ptr = std::static_pointer_cast<CartesianConstraint>(constraint->second);
    ptr->positionController()->setDesiredTrajectory(dummy,
                                                    rightFootTwist.getLinearVec3(),
                                                    rightFootToWorldTransform.getPosition());

    ptr->orientationController()->setDesiredTrajectory(dummy,
                                                       rightFootTwist.getAngularVec3(),
                                                       rightFootToWorldTransform.getRotation());

    if(leftFootToWorldTransform.getPosition()(2) > 0)
        yInfo() << "left detached";
    if(rightFootToWorldTransform.getPosition()(2) > 0)
        yInfo() << "left detached";

    return true;
}


bool WalkingTaskBasedTorqueController_osqp::setFeetState(const iDynTree::Transform& leftFootToWorldTransform,
                                                         const iDynTree::Twist& leftFootTwist,
                                                         const iDynTree::Transform& rightFootToWorldTransform,
                                                         const iDynTree::Twist& rightFootTwist)
{
    m_leftFootToWorldTransform = leftFootToWorldTransform;
    m_rightFootToWorldTransform = rightFootToWorldTransform;

    std::shared_ptr<CartesianConstraint> ptr;

    // left foot
    auto constraint = m_constraints.find("left_foot");
    if(constraint == m_constraints.end())
    {
        yError() << "[setFeetState] unable to find the left foot constraint. "
                 << "Please call 'initialize()' method";
        return false;
    }
    ptr = std::static_pointer_cast<CartesianConstraint>(constraint->second);
    ptr->positionController()->setFeedback(leftFootTwist.getLinearVec3(),
                                           leftFootToWorldTransform.getPosition());

    ptr->orientationController()->setFeedback(leftFootTwist.getAngularVec3(),
                                              leftFootToWorldTransform.getRotation());

    // right foot
    constraint = m_constraints.find("right_foot");
    if(constraint == m_constraints.end())
    {
        yError() << "[setFeetState] unable to find the right foot constraint. "
                 << "Please call 'initialize()' method";
        return false;
    }
    ptr = std::static_pointer_cast<CartesianConstraint>(constraint->second);
    ptr->positionController()->setFeedback(rightFootTwist.getLinearVec3(),
                                           rightFootToWorldTransform.getPosition());

    ptr->orientationController()->setFeedback(rightFootTwist.getAngularVec3(),
                                              rightFootToWorldTransform.getRotation());

    return true;
}

bool WalkingTaskBasedTorqueController_osqp::setFeetJacobian(const iDynTree::MatrixDynSize& leftFootJacobian,
                                                            const iDynTree::MatrixDynSize& rightFootJacobian)
{
    // set the feet jacobian
    if(leftFootJacobian.rows() != 6)
    {
        yError() << "[setFeetJacobian] the number of rows has to be equal to 6.";
        return false;
    }
    if(leftFootJacobian.cols() != m_actuatedDOFs + 6)
    {
        yError() << "[setFeetJacobian] the number of columns has to be equal to"
                 << m_actuatedDOFs + 6;
        return false;
    }

    if(rightFootJacobian.rows() != 6)
    {
        yError() << "[setFeetJacobian] the number of rows has to be equal to 6.";
        return false;
    }
    if(rightFootJacobian.cols() != m_actuatedDOFs + 6)
    {
        yError() << "[setFeetJacobian] the number of columns has to be equal to"
                 << m_actuatedDOFs + 6;
        return false;
    }

    m_rightFootJacobian = rightFootJacobian;
    m_leftFootJacobian = leftFootJacobian;

    return true;
}

void WalkingTaskBasedTorqueController_osqp::setFeetBiasAcceleration(const iDynTree::Vector6 &leftFootBiasAcceleration,
                                                                    const iDynTree::Vector6 &rightFootBiasAcceleration)
{

    iDynTree::toEigen(m_leftFootBiasAcceleration) = iDynTree::toEigen(leftFootBiasAcceleration);
    iDynTree::toEigen(m_rightFootBiasAcceleration) = iDynTree::toEigen(rightFootBiasAcceleration);
}


bool WalkingTaskBasedTorqueController_osqp::setDesiredCoMTrajectory(const iDynTree::Position& comPosition,
                                                                    const iDynTree::Vector3& comVelocity,
                                                                    const iDynTree::Vector3& comAcceleration)
{
    std::shared_ptr<CartesianConstraint> ptr;

    // save com desired trajectory
    auto constraint = m_constraints.find("com");
    if(constraint == m_constraints.end())
    {
        yError() << "[setDesiredCoMTrajectory] unable to find the com constraint. "
                 << "Please call 'initialize()' method";
        return false;
    }
    ptr = std::static_pointer_cast<CartesianConstraint>(constraint->second);
    ptr->positionController()->setDesiredTrajectory(comAcceleration, comVelocity, comPosition);

    return true;
}

bool WalkingTaskBasedTorqueController_osqp::setCoMState(const iDynTree::Position& comPosition,
                                                        const iDynTree::Vector3& comVelocity)
{
    auto constraint = m_constraints.find("com");
    if(constraint == m_constraints.end())
    {
        yError() << "[setCoMState] unable to find the right foot constraint. "
                 << "Please call 'initialize()' method";
        return false;
    }
    auto ptr = std::static_pointer_cast<CartesianConstraint>(constraint->second);
    ptr->positionController()->setFeedback(comVelocity, comPosition);

    return true;
}

bool WalkingTaskBasedTorqueController_osqp::setCoMJacobian(const iDynTree::MatrixDynSize& comJacobian)
{
    // set the feet jacobian
    if(comJacobian.rows() != 3)
    {
        yError() << "[setCoMJacobian] the number of rows has to be equal to 3.";
        return false;
    }
    if(comJacobian.cols() != m_actuatedDOFs + 6)
    {
        yError() << "[setCoMJacobian] the number of columns has to be equal to"
                 << m_actuatedDOFs + 6;
        return false;
    }

    m_comJacobian = comJacobian;

    return true;
}

bool WalkingTaskBasedTorqueController_osqp::setCoMBiasAcceleration(const iDynTree::Vector3 &comBiasAcceleration)
{
    iDynTree::toEigen(m_comBiasAcceleration) = iDynTree::toEigen(comBiasAcceleration);
    return true;
}

bool WalkingTaskBasedTorqueController_osqp::setDesiredZMP(const iDynTree::Vector2 &zmp)
{
    // std::shared_ptr<ZMPConstraint> ptr;

    // auto constraint = m_constraints.find("zmp");
    // if(constraint == m_constraints.end())
    // {
    //     yError() << "[setDesiredZMP] Unable to find the zmp constraint. "
    //              << "Please call 'initialize()' method";
    //     return false;
    // }
    // ptr = std::static_pointer_cast<ZMPConstraint>(constraint->second);
    // ptr->setDesiredZMP(zmp);

    return true;
}

bool WalkingTaskBasedTorqueController_osqp::setFeetState(const bool &leftInContact,
                                                         const bool &rightInContact)
{
    std::shared_ptr<ForceConstraint> ptr;

    auto constraint = m_constraints.find("left_force");
    if(constraint == m_constraints.end())
    {
        yError() << "[setFeetState] Unable to find the left_force constraint. "
                 << "Please call 'initialize()' method";
        return false;
    }
    ptr = std::static_pointer_cast<ForceConstraint>(constraint->second);
    if(leftInContact)
        ptr->activate();
    else
    {
        ptr->deactivate();
        yInfo() << "[deactivate left]";
    }

    constraint = m_constraints.find("right_force");
    if(constraint == m_constraints.end())
    {
        yError() << "[setFeetState] Unable to find the right_force constraint. "
                 << "Please call 'initialize()' method";
        return false;
    }
    ptr = std::static_pointer_cast<ForceConstraint>(constraint->second);
    if(rightInContact)
    {
        ptr->activate();
    }
    else
    {
        ptr->deactivate();
        yInfo() << "[deactivate right]";
    }

    return true;
}

bool WalkingTaskBasedTorqueController_osqp::setHessianMatrix()
{
    // neck orientation
    iDynTree::toEigen(m_neckHessianSubMatrix) = iDynTree::toEigen(m_neckJacobian).transpose() *
        iDynTree::toEigen(m_neckOrientationWeight).asDiagonal() * iDynTree::toEigen(m_neckJacobian);

    m_neckHessianSubMatrixTriplets.setSubMatrix(0, 0, m_neckHessianSubMatrix);
    m_neckHessian.setFromTriplets(m_neckHessianSubMatrixTriplets);


    // evaluate the hessian matrix
    m_hessianEigen = iDynTree::toEigen(m_neckHessian) + iDynTree::toEigen(m_jointRegularizationHessian)
        + iDynTree::toEigen(m_inputRegularizationHessian);

    if(m_optimizer->isInitialized())
    {
        if(!m_optimizer->updateHessianMatrix(m_hessianEigen))
        {
            yError() << "[setHessianMatrix] Unable to update the hessian matrix.";
            return false;
        }
    }
    else
    {
        if(!m_optimizer->data()->setHessianMatrix(m_hessianEigen))
        {
            yError() << "[setHessianMatrix] Unable to set first time the hessian matrix.";
            return false;
        }
    }

    return true;
}

bool WalkingTaskBasedTorqueController_osqp::setGradientVector()
{
    // regularization joint
    iDynTree::toEigen(m_desiredJointAccelerationController)
        = iDynTree::toEigen(m_desiredJointAcceleration)
        + iDynTree::toEigen(m_jointRegularizationDerivativeGains).asDiagonal() *
        (iDynTree::toEigen(m_desiredJointVelocity) - iDynTree::toEigen(m_jointVelocity))
        + iDynTree::toEigen(m_jointRegularizationProportionalGains).asDiagonal() *
        (iDynTree::toEigen(m_desiredJointPosition) - iDynTree::toEigen(m_jointPosition));

    // neck orientation
    m_neckOrientationController->evaluateControl();
    auto neckDesiredAcceleration = m_neckOrientationController->getControl();

    iDynTree::toEigen(m_neckGradientSubMatrix) = iDynTree::toEigen(m_neckJacobian).transpose() *
        iDynTree::toEigen(m_neckOrientationWeight).asDiagonal();

    m_neckGradientSubMatrixTriplets.setSubMatrix(0, 0, m_neckGradientSubMatrix);
    m_neckGradient.setFromTriplets(m_neckGradientSubMatrixTriplets);

    m_gradient =
        - iDynTree::toEigen(m_jointRegularizationGradient) * iDynTree::toEigen(m_desiredJointAcceleration)
        - iDynTree::toEigen(m_neckGradient) * (iDynTree::toEigen(neckDesiredAcceleration) -
                                               iDynTree::toEigen(m_neckBiasAcceleration))
        - iDynTree::toEigen(m_inputRegularizationGradient) * m_solution.block(m_actuatedDOFs + 6, 0, m_actuatedDOFs + 12, 1);

    if(m_optimizer->isInitialized())
    {
        if(!m_optimizer->updateGradient(m_gradient))
        {
            yError() << "[setGradient] Unable to update the gradient.";
            return false;
        }
    }
    else
    {
        if(!m_optimizer->data()->setGradient(m_gradient))
        {
            yError() << "[setGradient] Unable to set first time the gradient.";
            return false;
        }
    }

    return true;
}

bool WalkingTaskBasedTorqueController_osqp::setLinearConstraintMatrix()
{
    for(const auto& constraint: m_constraints)
    {
        m_profiler->setInitTime("add A");
        constraint.second->evaluateJacobian(m_constraintMatrixEigen);
        m_profiler->setEndTime("add A");
    }

    if(m_optimizer->isInitialized())
    {
        if(!m_optimizer->updateLinearConstraintsMatrix(m_constraintMatrixEigen))
        {
            yError() << "[setLinearConstraintsMatrix] Unable to update the constraints matrix.";
            return false;
        }
    }
    else
    {
        if(!m_optimizer->data()->setLinearConstraintsMatrix(m_constraintMatrixEigen))
        {
            yError() << "[setLinearConstraintsMatrix] Unable to set the constraints matrix.";
            return false;
        }
    }

    return true;
}

bool WalkingTaskBasedTorqueController_osqp::setBounds()
{
    for(const auto& constraint: m_constraints)
    {
        constraint.second->evaluateBounds(m_upperBound, m_lowerBound);
    }

    // yInfo() << "m_lower real";
    // std::cerr<<m_lowerBound<<"\n";

    // yInfo() << "m_upper real";
    // std::cerr<<m_upperBound<<"\n";

    if(m_optimizer->isInitialized())
    {
        if(!m_optimizer->updateBounds(m_lowerBound, m_upperBound))
        {
            yError() << "[setBounds] Unable to update the bounds.";
            return false;
        }
    }
    else
    {
        if(!m_optimizer->data()->setLowerBound(m_lowerBound))
        {
            yError() << "[setBounds] Unable to set the first time the lower bound.";
            return false;
        }

        if(!m_optimizer->data()->setUpperBound(m_upperBound))
        {
            yError() << "[setBounds] Unable to set the first time the upper bound.";
            return false;
        }
    }

    return true;
}

bool WalkingTaskBasedTorqueController_osqp::solve()
{
    m_profiler->setInitTime("hessian");

    if(!setHessianMatrix())
    {
        yError() << "[solve] Unable to set the hessian matrix.";
        return false;
    }

    m_profiler->setEndTime("hessian");

    m_profiler->setInitTime("gradient");

    if(!setGradientVector())
    {
        yError() << "[solve] Unable to set the gradient vector matrix.";
        return false;
    }

    m_profiler->setEndTime("gradient");

    m_profiler->setInitTime("A");

    if(!setLinearConstraintMatrix())
    {
        yError() << "[solve] Unable to set the linear constraint matrix.";
        return false;
    }

    m_profiler->setEndTime("A");

    m_profiler->setInitTime("bounds");
    if(!setBounds())
    {
        yError() << "[solve] Unable to set the bounds.";
        return false;
    }

    m_profiler->setEndTime("bounds");

    m_profiler->setInitTime("solve");
    if(!m_optimizer->isInitialized())
    {
        if(!m_optimizer->initSolver())
        {
            yError() << "[solve] Unable to initialize the solver";
            return false;
        }
    }

    if(!m_optimizer->solve())
    {
        yError() << "[solve] Unable to solve the problem.";
        return false;
    }

    m_solution = m_optimizer->getSolution();

    // check equality constraints
    if(!isSolutionFeasible())
    {
        yError() << "[solve] The solution is not feasible.";
        return false;
    }

    m_profiler->setEndTime("solve");

    m_profiler->profiling();

    m_isSolutionEvaluated = true;
    return true;
}

bool WalkingTaskBasedTorqueController_osqp::isSolutionFeasible()
{
    double tolerance = 1;
    Eigen::VectorXd constrainedOutput = m_constraintMatrixEigen * m_solution;
    // std::cerr<<"m_constraintMatrix\n";
    // std::cerr<<m_constraintMatrixEigen<<std::endl;

    // std::cerr<<"upper\n";
    // std::cerr<<constrainedOutput - m_upperBound<<std::endl;

    // std::cerr<<"lower\n";
    // std::cerr<<constrainedOutput - m_lowerBound<<std::endl;

    // std::cerr<<"solution\n";
    // std::cerr<<m_solution<<"\n";

    if(((constrainedOutput - m_upperBound).maxCoeff() < tolerance)
       ||((constrainedOutput - m_lowerBound).maxCoeff() > -tolerance))
        return true;

    yError() << "[isSolutionFeasible] The constraints are not satisfied.";
    return false;
}


bool WalkingTaskBasedTorqueController_osqp::getSolution(iDynTree::VectorDynSize& output)
{
    if(!m_isSolutionEvaluated)
    {
        yError() << "[getSolution] The solution is not evaluated. Please call 'solve()' method.";
        return false;
    }

    if(output.size() != m_actuatedDOFs)
        output.resize(m_actuatedDOFs);

    // take only the joint torque
    for(int i = 0; i < m_actuatedDOFs; i++)
        output(i) = m_solution(i + m_actuatedDOFs + 6);

    m_isSolutionEvaluated = false;
    return true;
}


iDynTree::Wrench WalkingTaskBasedTorqueController_osqp::getLeftWrench()
{
    iDynTree::Wrench wrench;
    for(int i = 0; i < 6; i++)
        wrench(i) = m_solution(6 + m_actuatedDOFs + m_actuatedDOFs + i);

    return wrench;
}


iDynTree::Wrench WalkingTaskBasedTorqueController_osqp::getRightWrench()
{

    iDynTree::Wrench wrench;
    for(int i = 0; i < 6; i++)
        wrench(i) = m_solution(6 + m_actuatedDOFs + m_actuatedDOFs + 6 + i);

    return wrench;
}

iDynTree::Vector2 WalkingTaskBasedTorqueController_osqp::getZMP()
{
    iDynTree::Position zmpLeft, zmpRight, zmpWorld;
    double zmpLeftDefined = 0.0, zmpRightDefined = 0.0;

    iDynTree::Vector2 zmp;

    auto leftWrench = getLeftWrench();
    auto rightWrench = getRightWrench();

    if(rightWrench.getLinearVec3()(2) < 10)
        zmpRightDefined = 0.0;
    else
    {
        zmpRight(0) = -rightWrench.getAngularVec3()(1) / rightWrench.getLinearVec3()(2);
        zmpRight(1) = rightWrench.getAngularVec3()(0) / rightWrench.getLinearVec3()(2);
        zmpRight(2) = 0.0;
        zmpRightDefined = 1.0;
    }

    if(leftWrench.getLinearVec3()(2) < 10)
        zmpLeftDefined = 0.0;
    else
    {
        zmpLeft(0) = -leftWrench.getAngularVec3()(1) / leftWrench.getLinearVec3()(2);
        zmpLeft(1) = leftWrench.getAngularVec3()(0) / leftWrench.getLinearVec3()(2);
        zmpLeft(2) = 0.0;
        zmpLeftDefined = 1.0;
    }

    double totalZ = rightWrench.getLinearVec3()(2) + leftWrench.getLinearVec3()(2);

    zmpLeft = m_leftFootToWorldTransform * zmpLeft;
    zmpRight = m_rightFootToWorldTransform * zmpRight;

    // the global zmp is given by a weighted average
    iDynTree::toEigen(zmpWorld) = ((leftWrench.getLinearVec3()(2) * zmpLeftDefined) / totalZ)
        * iDynTree::toEigen(zmpLeft) +
        ((rightWrench.getLinearVec3()(2) * zmpRightDefined)/totalZ) * iDynTree::toEigen(zmpRight);

    zmp(0) = zmpWorld(0);
    zmp(1) = zmpWorld(1);

    return zmp;
}

iDynTree::Vector3 WalkingTaskBasedTorqueController_osqp::getDesiredNeckOrientation()
{
    return m_desiredNeckOrientation.asRPY();
}