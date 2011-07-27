#ifndef FINITE_STATE_MACHINE_H
#define FINITE_STATE_MACHINE_H

#include <map>
#include <vector>
#include <stdexcept>

namespace holepoke
{

typedef unsigned FSMEvent;
typedef unsigned FSMState;
typedef FSMEvent (*FSMStateRoutine)(void*);

extern const FSMEvent kFSMStopState;

class FiniteStateMachine
{
private:
	unsigned int _state;
	void* _userInfo;
	
	struct EventStatePair
	{
		FSMEvent event;
		FSMState state;
	};
	
	struct ltEventStatePair
	{
		bool operator()(EventStatePair p1, EventStatePair p2) const;
	};
	
	typedef std::map<EventStatePair,FSMState,ltEventStatePair> TransitionMap;
	
	TransitionMap _transitions;
	std::vector<FSMStateRoutine> _stateRoutines;
	
public:
	
	// Create a finite state machine with the given user info.
	FiniteStateMachine();
	
	// Set the userInfo. This pointer is passed as a parameter to FSMStateRoutine routines.
	void setUserInfo(void* userInfo);
	
	// Add a transition from state to newState when event occurs.
	void addTransition(FSMState state, FSMEvent event, FSMState newState);
	
	// Define the callback for a state. Returns the added state identifier.
	FSMState addState(FSMStateRoutine routine);
	
	// Run the state machine. The first state added is the start state. This method will continue until it reaches kFSMStopState. 
	void run();
};

}

#endif
