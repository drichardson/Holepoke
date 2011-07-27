#include "fsm.h"
#include <assert.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(WIN32)
#define snprintf _snprintf
#endif

namespace holepoke {

const FSMEvent kFSMStopState = UINT_MAX - 1;

bool FiniteStateMachine::ltEventStatePair::operator()(EventStatePair p1, EventStatePair p2) const
{
	if ( p1.event < p2.event )
	{
		return true;
	}
	else if ( p1.event == p2.event && p1.state < p2.state )
	{
		return true;
	}
	
	return false;
}

FiniteStateMachine::FiniteStateMachine()
{
	_state = 0;
	_userInfo = NULL;
}

void FiniteStateMachine::setUserInfo(void* userInfo)
{
	_userInfo = userInfo;
}

void FiniteStateMachine::addTransition(FSMState state, FSMEvent event, FSMState newState) {
	EventStatePair pair = { event, state };
	_transitions[pair] = newState;
}

FSMState FiniteStateMachine::addState(FSMStateRoutine routine)
{
	assert(routine);
	FSMState state = _stateRoutines.size();
	_stateRoutines.push_back(routine);
	return state;
}

void FiniteStateMachine::run()
{
	_state = 0;
	
	do
	{
		// Run the state routine
		FSMStateRoutine stateRoutine = _stateRoutines[_state];
		FSMEvent event = stateRoutine(_userInfo);
		
		// Lookup the next state
		EventStatePair pair = { event, _state };
		TransitionMap::const_iterator itr = _transitions.find(pair);
	
		if ( itr == _transitions.end() )
		{
			char msg[100];
			snprintf(msg, sizeof(msg), "No transition defined for event %d in state %d", event, _state);
			throw std::logic_error(msg);
		}
		
		_state = itr->second;
		
	} while ( _state != kFSMStopState );
}

} // namespace holepoke
