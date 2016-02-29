#include "simulator.h"
#include <fstream>
#include <iostream>

using namespace std;


Simulator::Simulator() {
    // initialize the particles
	reset();
	selected_particle = 0;
	feedback = false;
	ks = 50;
	kd = 30;
}

int Simulator::getNumParticles() {
    return mParticles.size();
}

Particle* Simulator::getParticle(int index) {
    return mParticles[index];
}

double Simulator::getTimeStep() {
    return mTimeStep;
}

void Simulator::reset() {
	mParticles.clear();
	mParticles.push_back(new Particle());
	mParticles.push_back(new Particle());
	W = Eigen::MatrixXd::Identity((mParticles.size() - 1) * 3, (mParticles.size() - 1) * 3);
	for (int i = 1; i < (mParticles.size() - 1); i++ ) {
		for (int xyz = 0; xyz < 3; xyz++) {
			W(i-1 + xyz, i - 1 + xyz) = 1.0/mParticles[i]->mMass;
		}
	}
	// cout << "TEST" << endl;

	// Init particle positions (default is 0, 0, 0)
	mParticles[0]->mPosition[0] = 0.0;
	mParticles[0]->mPosition[1] = 0.0;
	mParticles[0]->mPosition[2] = 0.0;

	mParticles[1]->mPosition[0] = 0.2;
	mParticles[1]->mPosition[1] = 0.0;
	mParticles[1]->mPosition[2] = 0.0;

	mTimeStep = 0.0003;
	forces.clear();
	gravity.resetParticlesImpacted();

    for (int i = 0; i < mParticles.size(); i++) {
        mParticles[i]->mVelocity.setZero();
        mParticles[i]->mAccumulatedForce.setZero();
		// everything has gravity
		gravity.addParticlesImpacted(i);
		// cout << gravity.getParticlesImpacted()[i] << endl;
    }
	forces.push_back(&gravity);
	// cout << (*forces.begin())->getAcceleration() << endl;
	constraints.clear();
	constraints.push_back(Constraint(mParticles[0], mParticles[1]));
}

int Simulator::getSelectedParticle(){
	return selected_particle;
}

void Simulator::addParticle(float x_pos, float y_pos){
	// forces[0] will always be gravity
	forces[0]->addParticlesImpacted(mParticles.size());
	mParticles.push_back(new Particle(x_pos, y_pos));
	// update W matrix
	W = Eigen::MatrixXd::Identity((mParticles.size() - 1) * 3, (mParticles.size() - 1) * 3);
	for (int i = 1; i < (mParticles.size() - 1); i++) {
		for (int xyz = 0; xyz < 3; xyz++) {
			W(i - 1 + xyz, i - 1 + xyz) = 1.0 / mParticles[i]->mMass;
		}
	}
	// add new constraint
	constraints.push_back(Constraint(mParticles[mParticles.size()-2], mParticles[mParticles.size() - 1]));
}

bool Simulator::hasFeedback(){
	return feedback;
}

void Simulator::toggleFeedback(){
	feedback = !feedback;
}


void Simulator::simulate() {
	// clear force accumulator from previous iteration and update applied forces here
	for (int i = 0; i < mParticles.size(); i++) {
		mParticles[i]->mAccumulatedForce.setZero();
		mParticles[i]->fhat.setZero();
		mParticles[i]->update_accumulated_forces(i, forces);
	}
	
	Eigen::MatrixXd jacobian = Eigen::MatrixXd::Zero(constraints.size(), (mParticles.size()-1)*3);
	Eigen::MatrixXd jacobianDot = Eigen::MatrixXd::Zero(constraints.size(), (mParticles.size() - 1) * 3);;

	// not including the center circle which is at index 0 ; the center circle is a fixed point
	for (int i = 1; i < mParticles.size(); i++) {
		for (int j = 0; j < constraints.size(); j++) {
			Constraint currConstraint = constraints[j];
			if (mParticles[i] == currConstraint.getParticle2()) {
				Eigen::Vector3d currJ = currConstraint.dCdx2();
				Eigen::Vector3d currJdot = currConstraint.dCdotdx2();
				for (int temp = 0; temp < 3; temp++) {
					jacobian(j, (i - 1) * 3 + temp) = currJ[temp];
					jacobianDot(j, (i - 1) * 3 + temp) = currJdot[temp];
				}
			}
			else if (mParticles[i] == currConstraint.getParticle1()) {
				Eigen::Vector3d currJ = currConstraint.dCdx1();
				Eigen::Vector3d currJdot = currConstraint.dCdotdx1();
				for (int temp = 0; temp < 3; temp++) {
					jacobian(j, (i - 1) * 3 + temp) = currJ[temp];
					jacobianDot(j, (i - 1) * 3 + temp) = currJdot[temp];
				}
			}
			
		}
	}

	// cout << jacobian << endl;

	Eigen::VectorXd Q((mParticles.size() - 1) * 3);
	Eigen::VectorXd qdot((mParticles.size() - 1) * 3);

	for (int i = 1; i < mParticles.size(); i++) {
		for (int temp = 0; temp < 3; temp++) {
			Q((i - 1) * 3 + temp) = mParticles[i]->mAccumulatedForce(temp);
			qdot((i - 1) * 3 + temp) = mParticles[i]->mVelocity(temp);
		}
	}
	
	// one lambda for each particle
	Eigen::MatrixXd tempCequation = -jacobianDot*qdot - jacobian*W*Q;
	// if feedback then need to so -ksC-kdCdot
	
		Eigen::VectorXd C(constraints.size());
		Eigen::VectorXd Cdot(constraints.size());
		for (int i = 0; i < constraints.size(); i++) {
			C(i) = 0.5*constraints[i].x2()-0.5;
			Cdot(i) = constraints[i].Cdot();
		}
		cout << Cdot << endl;
	if (feedback) {
		tempCequation = tempCequation - ks*C - kd*Cdot;
	}
	Eigen::MatrixXd lambda = (jacobian*W*(jacobian.transpose())).inverse()*(tempCequation);
	Eigen::MatrixXd legal_forces = jacobian.transpose()*lambda;
	
	/**
	cout << "JACOBIAN" << endl;
	cout << jacobian.rows() << endl;
	cout << jacobian.cols() << endl;
	cout << "W" << endl;
	cout << W.rows() << endl;
	cout << W.cols() << endl;
	cout << "qdot" << endl;
	cout << qdot.rows() << endl;
	cout << qdot.cols() << endl;
	cout << "Q" << endl;
	cout << Q.rows() << endl;
	cout << Q.cols() << endl;
	cout << lambda.rows() << endl;
	cout << lambda.cols() << endl;
	**/

	// cout << legal_forces << endl;
	
	for (int i = 1; i < mParticles.size(); i++) {
		mParticles[i]->fhat[0] = legal_forces((i - 1) * 3);
		mParticles[i]->fhat[1] = legal_forces((i - 1) * 3 + 1);
		mParticles[i]->fhat[2] = legal_forces((i - 1) * 3 + 2);
	}
	std::vector<Eigen::VectorXd> derivatives;
	derivatives.resize(mParticles.size()-1);

	for (int i = 1; i < mParticles.size(); i++) {
		derivatives[i-1] = solver.solve_X_dot(mParticles[i]);
		mParticles[i]->mPosition += Eigen::Vector3d(derivatives[i-1][0], derivatives[i - 1][1], derivatives[i - 1][2]) * mTimeStep;
		mParticles[i]->mVelocity += Eigen::Vector3d(derivatives[i-1][3], derivatives[i - 1][4], derivatives[i - 1][5]) * mTimeStep;
	}

}

