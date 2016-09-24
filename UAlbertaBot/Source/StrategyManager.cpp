#include "Common.h"
#include "StrategyManager.h"
#include "UnitUtil.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <string>

using namespace UAlbertaBot;

// constructor
StrategyManager::StrategyManager() 
	: _selfRace(BWAPI::Broodwar->self()->getRace())
	, _enemyRace(BWAPI::Broodwar->enemy()->getRace())
    , _emptyBuildOrder(BWAPI::Broodwar->self()->getRace())
{
	//initialise strategy constants
	
	_fucs = 3;
	_fuce = 18;
	_fbcs = 19;
	_fbce = 34;
	_fups = 35;
	_fupe = 53;
	_eucs = 54;
	_euce = 69;
	_ebcs = 70;
	_ebce = 85;
	_eups = 86;
	_eupe = 104;
	_gameNum = 2;
	bestState = -1;
	goalState = -1;
	bestStateStr = "";
	goalStateStr = "";
	numGames = 0;
	openingGame = 0;
	estimatedState = std::vector<int>(_eupe, 0);
	std::string dbLocation{ "C:/Program Files/Starcraft/bwapi-data/AI/PvP_db2.txt" };
	//Initialise the database
	if (BWAPI::Broodwar->enemy()->getRace() == BWAPI::Races::Terran) {
		_ebce = 100; //enemy count is at a different position~
		std::string dbLocation{ "C:/Program Files/Starcraft/bwapi-data/AI/PvT_db.txt" };
	}
	dbFile.open(dbLocation);
	if (dbFile.is_open()) {
		BWAPI::Broodwar->printf("Database at %s found.\n", dbLocation);
		std::string fileStr;
		int updateGame = -1;
		//parse the database into a managebale double vector.
		while (std::getline(dbFile, fileStr)) {
			std::vector<std::string> state = split(fileStr, ',');
			std::string buildStr = state.back();
			buildStr.erase(std::remove(buildStr.begin(), buildStr.end(), '\n'), buildStr.end());
			buildStr.erase(std::remove(buildStr.begin(), buildStr.end(), '\r'), buildStr.end());
			stateMoves.push_back(buildStr); //get the move (last element)
			state.pop_back();
			std::vector<int> intState;
			
			for (auto it = state.begin(); it != state.end(); ++it) { //convert string vector to int vector
				std::string unitStr = *it;
				intState.push_back(atoi(unitStr.c_str()));
			}
			//for (int j = 0; j < state.size(); ++j){
				//intState[j] = atoi(state[j].c_str());
			//}

			if (updateGame != intState[_gameNum]) { //update the number of games
				numGames++;
				updateGame = intState[_gameNum];
			}

			pastStates.push_back(intState); //push the actual state
		}
		BWAPI::Broodwar->printf("Database info parsed into game.\n", dbLocation);
		dbFile.close();

	}
	else {
		BWAPI::Broodwar->printf("Could not find file at %s\n", dbLocation);
	}

	
}

// get an instance of this
StrategyManager & StrategyManager::Instance() 
{
	static StrategyManager instance;
	return instance;
}

const int StrategyManager::getScore(BWAPI::Player player) const
{
	return player->getBuildingScore() + player->getKillScore() + player->getRazingScore() + player->getUnitScore();
}

const BuildOrder & StrategyManager::getOpeningBookBuildOrder() const
{
    auto buildOrderIt = _strategies.find(Config::Strategy::StrategyName);
	//StrategyManager::Instance().predict();

    // look for the build order in the build order map
	if (buildOrderIt != std::end(_strategies))
    {
        return (*buildOrderIt).second._buildOrder;
    }
    else
    {
        UAB_ASSERT_WARNING(false, "Strategy not found: %s, returning empty initial build order", Config::Strategy::StrategyName.c_str());
        return _emptyBuildOrder;
    }
}

const bool StrategyManager::shouldExpandNow() const
{
	// if there is no place to expand to, we can't expand
	if (MapTools::Instance().getNextExpansion() == BWAPI::TilePositions::None)
	{
        BWAPI::Broodwar->printf("No valid expansion location");
		return false;
	}

	size_t numDepots    = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Command_Center)
                        + UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Nexus)
                        + UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hatchery)
                        + UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Lair)
                        + UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hive);
	int frame           = BWAPI::Broodwar->getFrameCount();
    int minute          = frame / (24*60);

	// if we have a ton of idle workers then we need a new expansion
	if (WorkerManager::Instance().getNumIdleWorkers() > 10)
	{
		return true;
	}

    // if we have a ridiculous stockpile of minerals, expand
    if (BWAPI::Broodwar->self()->minerals() > 3000)
    {
        return true;
    }

    // we will make expansion N after array[N] minutes have passed
    std::vector<int> expansionTimes = {5, 10, 20, 30, 40 , 50};

    for (size_t i(0); i < expansionTimes.size(); ++i)
    {
        if (numDepots < (i+2) && minute > expansionTimes[i])
        {
            return true;
        }
    }

	return false;
}

void StrategyManager::addStrategy(const std::string & name, Strategy & strategy)
{
    _strategies[name] = strategy;
}

const MetaPairVector StrategyManager::getBuildOrderGoal()
{
    BWAPI::Race myRace = BWAPI::Broodwar->self()->getRace();

    if (myRace == BWAPI::Races::Protoss)
    {
        return getProtossBuildOrderGoal();
    }
    else if (myRace == BWAPI::Races::Terran)
	{
		return getTerranBuildOrderGoal();
	}
    else if (myRace == BWAPI::Races::Zerg)
	{
		return getZergBuildOrderGoal();
	}

    return MetaPairVector();
}

BuildOrder StrategyManager::selectOpeningStrategy() {
	std::srand(time(NULL));
	int randGame = std::rand() % numGames;
	BWAPI::Broodwar->printf("Num games: %d. game selected: %d", numGames, randGame);
	bestState = 0;
	//17102 = zealot rush
	//0 = dragoon rush
	if (Config::Tools::RandomStartingStrat) {
		openingGame = randGame;
		for (int pi = 0; pi < pastStates.size(); ++pi) {
			if (pastStates[pi][_gameNum] == randGame) {
				bestState = pi;
				break;
			}
		}
	}
	return getBuildOrderFromState(bestState, Config::Tools::OpeningWindow);
}

//Using opponents current state, match it up with the database and return a build order of the next 15 steps
BuildOrder StrategyManager::predict() {


	//===== Get enemy's current state ========
	std::string strTrace;
	estimatedState = std::vector<int>(_eupe, 0);

	//std::string testStr;
	int iunit = 0;

	iunit = _fucs;
	//update friendly unit counts
	for (BWAPI::UnitType t : BWAPI::UnitTypes::allUnitTypes())
	{

		int numUnits = InformationManager::Instance().getUnitData(BWAPI::Broodwar->self()).getNumUnits(t);
		if (!t.isBuilding() && !t.isHero() && !t.isSpell() && t.getRace() == BWAPI::Broodwar->self()->getRace()) {
			estimatedState[iunit] = numUnits;
			iunit++;
		}

	}


	iunit = _fbcs;
	//update friendly building counts
	for (BWAPI::UnitType t : BWAPI::UnitTypes::allUnitTypes())
	{

		int numUnits = InformationManager::Instance().getUnitData(BWAPI::Broodwar->self()).getNumUnits(t);
		if (t.isBuilding() && !t.isSpecialBuilding() && t.getRace() == BWAPI::Broodwar->self()->getRace())
		{
			estimatedState[iunit] = numUnits;
			iunit++;
		}

	}

	iunit = _ebcs;
	//update enemy building counts
	for (BWAPI::UnitType t : BWAPI::UnitTypes::allUnitTypes())
	{
		
		int numUnits = InformationManager::Instance().getUnitData(BWAPI::Broodwar->enemy()).getNumUnits(t);	
		if (t.isBuilding() && !t.isSpecialBuilding() && t.getRace() == BWAPI::Broodwar->enemy()->getRace())
		{
			estimatedState[iunit] = numUnits;
			iunit++;
		}
		
	}
	iunit = _eucs;
	//update enemy unit counts
	for (BWAPI::UnitType t : BWAPI::UnitTypes::allUnitTypes())
	{
		
		int numUnits = InformationManager::Instance().getUnitData(BWAPI::Broodwar->enemy()).getNumUnits(t);
		if (!t.isBuilding() && !t.isHero() && !t.isSpell() && t.getRace() == BWAPI::Broodwar->enemy()->getRace()) {
			estimatedState[iunit] = numUnits;
			iunit++;
		}
		
	}

	iunit = _eupe;
	for (BWAPI::UpgradeType t : BWAPI::UpgradeTypes::allUpgradeTypes())
	{
		if (t.getRace() == BWAPI::Broodwar->self()->getRace()) {
			if (BWAPI::Broodwar->self()->getUpgradeLevel(t)) {
				estimatedState[iunit] = 5000;
			}
			iunit++;
		}

	}
	calcError();

	iunit = _eucs;

	//===== Search Database States and find matching =====

	bestState = -1;
	bestStateStr = "";
	//int bestContState{ -1 }; //best continuous state out of all the games 0 to ~100,000 states
	
	double minDist = 99999;

	for(int pi = 0; pi < pastStates.size(); ++pi) {
		// output the line		
		//std::vector<int> state = split(fileStr, ',');
		int ei{ 0 }; //enemy state counter

		double dist = calcStateDist(pastStates[pi]);

		if (dist < minDist) {
			minDist = dist;
			bestState = pi;
		}
	}


	return getBuildOrderFromState(bestState, Config::Tools::Window);
}

double StrategyManager::calcStateDist(std::vector<int> state){

	//=====calculate basic euclidean distance ====
	double distance{ 0 };
	//friendly unit count
	int wu = Config::Tools::wu;
	int wb = Config::Tools::wb;
	int we = Config::Tools::we;
	int wf = Config::Tools::wf;
	//int _pylon = 21;

	if (Config::Tools::fuc) {
		for (int ui = _fucs; ui <= _fuce; ++ui) {
			distance += (wu + wf) * eucDistance(state[ui], estimatedState[ui]);
		}
	}
	//friendly building count
	if (Config::Tools::fbc) {
		for (int ui = _fbcs; ui <= _fbce; ++ui) {
			//if (ui == _pylon) {
			//	distance += (wu + wf)*eucDistance(state[ui], estimatedState[ui]); //treat pylons as units
		//	} else {
				distance += (wb + wf)*eucDistance(state[ui], estimatedState[ui]);
			//}
		}
	}
	//enemy building count
	if (Config::Tools::ebc) {
		for (int ui = _ebcs; ui <= _ebce; ++ui) {
			distance += (wb + we)*eucDistance(state[ui], estimatedState[ui]);
		}
	}
	//enemy unit count
	if (Config::Tools::euc) {
		for (int ui = _eucs; ui <= _euce; ++ui) {
			distance += (wu + wf)*eucDistance(state[ui], estimatedState[ui]);
		}
	}
	return distance;
}

//Calculate the actual error between where we expected the opponent to be and where they actually are now.
void StrategyManager::calcError() {
	if (!prevPredictedState.empty()) {
		double numStates = 0;
		double err = 0;
		int totalError = 0;
		for (int i = _eucs; i <= _ebce; ++i) {
			numStates++;
			err = prevPredictedState[i] - estimatedState[i];
			totalError += (err*err);
		}
		double rmsError = sqrt(totalError / numStates);
		if (rmse.empty()) {
			rmse.push_back(rmsError);
		}
		else if (rmsError != rmse.back()) {
			rmse.push_back(rmsError);
		}
	}
}

BuildOrder StrategyManager::getBuildOrderFromState(int bestState, int window){

	while ((bestState + window) > pastStates.size()) --window; //Make sure that the predicted state does not go outside of our pastStates vector
	while (pastStates[bestState][_gameNum] != pastStates[bestState + window][_gameNum]) --window; //decrease the window size

	goalState = bestState + window ;


	goalStateStr = "";
	bestStateStr = "";
	estimatedStateStr = "";
	for (int ui = _fucs; ui <= _fupe; ++ui) {
		goalStateStr.append(std::to_string(pastStates[goalState][ui]));
		goalStateStr.append(" ");
		bestStateStr.append(std::to_string(pastStates[bestState][ui]));
		bestStateStr.append(" ");
		estimatedStateStr.append(std::to_string(estimatedState[ui]));
		estimatedStateStr.append(" ");
	}
	for (int ui = _eucs; ui <= _eupe; ++ui) {
		goalStateStr.append(std::to_string(pastStates[goalState][ui]));
		goalStateStr.append(" ");
		bestStateStr.append(std::to_string(pastStates[bestState][ui]));
		bestStateStr.append(" ");
		estimatedStateStr.append(std::to_string(estimatedState[ui]));
		estimatedStateStr.append(" ");
	}

	//BWAPI::Broodwar->printf("LINE: %s", stateMoves[predictedState].c_str());
	BuildOrder buildOrder(BWAPI::Broodwar->self()->getRace());

	for (int ii = bestState; ii < goalState; ++ii) {
		buildOrder.add(MetaType(stateMoves[ii]));
		//goalOrder.add(BWAPI::UnitTypes::Protoss_Probe);
	}

	prevPredictedState = pastStates[goalState];

	return buildOrder;
}

void StrategyManager::drawPrediction() {

	int predictStratX = 20;
	int predictStratY = 230;
	BWAPI::Broodwar->drawTextScreen(predictStratX, predictStratY, "current estimated state:");
	BWAPI::Broodwar->drawTextScreen(predictStratX, predictStratY + 10, "<%s>", estimatedStateStr);
	//BWAPI::Broodwar->printf("best state: %d of game %d", bestContState, bestGame);
	BWAPI::Broodwar->drawTextScreen(predictStratX, predictStratY+20, "best matching state: %d", bestState);
	BWAPI::Broodwar->drawTextScreen(predictStratX, predictStratY+30, "<%s>", bestStateStr);
	BWAPI::Broodwar->drawTextScreen(predictStratX, predictStratY + 40, "goal state: %d", goalState);
	BWAPI::Broodwar->drawTextScreen(predictStratX, predictStratY + 50, "<%s>", goalStateStr);
	//BWAPI::Broodwar->printf("enemy state <%s>", strTrace.c_str());
	//BWAPI::Broodwar->drawTextScreen(predictStratX, predictStratY+20, "goal state: %d of game %d", goalState, pastStates[goalState][_gameNum]);
	
}

//basic state search heuristic
double StrategyManager::eucDistance(const double lhs, const double rhs) {
	return sqrt((lhs - rhs) * (lhs - rhs));
}

//Split a string of ints based on a delimeter
//Used to turn database states into a vector of ints for processing.
std::vector<std::string> StrategyManager::split(const std::string &s, char delim) {
	std::stringstream ss(s);
	std::string item;
	std::vector<std::string> tokens;
	while (getline(ss, item, delim)) {
		tokens.push_back(item);
	}
	return tokens;
}

const MetaPairVector StrategyManager::getProtossBuildOrderGoal() const
{
	// the goal to return
	MetaPairVector goal;


	int numZealots = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Zealot);
	int numPylons = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Pylon);
	int numGateways = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Gateway);
	int numDragoons = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Dragoon);
	int numProbes = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Probe);
	int numNexusCompleted = BWAPI::Broodwar->self()->completedUnitCount(BWAPI::UnitTypes::Protoss_Nexus);
	int numNexusAll = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Nexus);
	int numCyber = BWAPI::Broodwar->self()->completedUnitCount(BWAPI::UnitTypes::Protoss_Cybernetics_Core);
	int numCannon = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Photon_Cannon);
	int numScout = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Corsair);
	int numReaver = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Reaver);
	int numDarkTeplar = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Dark_Templar);
	//BuildOrder buildOrder = StrategyManager::Instance().predict();
	if (Config::Strategy::StrategyName == "NewSouthBot") { // Use the prediction strategy
		BuildOrder buildOrder = StrategyManager::Instance().predict();
		/*for (size_t i(0); i<buildOrder.size(); ++i) //get the build order and push all the metaTypes to the goal
		{
			BWAPI::Broodwar->printf("PUSHING: %s\n", buildOrder[i].getName().c_str());
			if (buildOrder[i].type() == BWAPI::UnitTypes::Protoss_Dragoon) {
				goal.push_back(MetaPair(buildOrder[i], numDragoons + 1));
				numDragoons++;
			} else if (buildOrder[i].getUnitType() == BWAPI::UnitTypes::Protoss_Zealot) {
				goal.push_back(MetaPair(buildOrder[i], numZealots + 1));
				numZealots++;
			} else if (buildOrder[i].getUnitType() == BWAPI::UnitTypes::Protoss_Dark_Templar) {
				goal.push_back(MetaPair(buildOrder[i], numDarkTeplar + 1));
				numDarkTeplar++;
			}
			else if (buildOrder[i].getUnitType() == BWAPI::UnitTypes::Protoss_Pylon) {
				goal.push_back(MetaPair(buildOrder[i], numPylons + 1));
				numPylons++;
			}
			else if (buildOrder[i].getUnitType() == BWAPI::UnitTypes::Protoss_Pylon) {
				goal.push_back(MetaPair(buildOrder[i], numGateways + 1));
				numGateways++;
			}
			else if (buildOrder[i].isUpgrade()) {
				goal.push_back(MetaPair(buildOrder[i], 1));
			} else {
				int numUnits = 0;
				numUnits = UnitUtil::GetAllUnitCount(buildOrder[i].getUnitType());
				goal.push_back(MetaPair(buildOrder[i], numUnits + 1));
			}
		} */
		//goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Dragoon, numDragoons + 2));
		//goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Zealot, numZealots + 1));
		
		//update friendly unit counts

		int iunit = _fucs;
		for (BWAPI::UnitType t : BWAPI::UnitTypes::allUnitTypes())
		{
			if (!t.isBuilding() && !t.isHero() && !t.isSpell() && t.getRace() == BWAPI::Broodwar->self()->getRace()) {
				int numUnits = InformationManager::Instance().getUnitData(BWAPI::Broodwar->self()).getNumUnits(t);
				int addUnits = pastStates[goalState][iunit] - estimatedState[iunit];
				if (addUnits > 0) {
					goal.push_back(MetaPair(t, numUnits + addUnits));
				}
				iunit++;
			}

		}

		iunit = _fbcs;
		//update friendly building counts
		for (BWAPI::UnitType t : BWAPI::UnitTypes::allUnitTypes())
		{
			if (t.isBuilding() && !t.isSpecialBuilding() && t.getRace() == BWAPI::Broodwar->self()->getRace())
			{
				int numUnits = InformationManager::Instance().getUnitData(BWAPI::Broodwar->self()).getNumUnits(t);
				int addUnits = pastStates[goalState][iunit] - estimatedState[iunit];
				if (addUnits > 0) {
					goal.push_back(MetaPair(t, numUnits + addUnits));
				}
				iunit++;
			}

		}

		//update upgrades

		iunit = _fups;
		for (BWAPI::UpgradeType t : BWAPI::UpgradeTypes::allUpgradeTypes())
		{
			if (t.getRace() == BWAPI::Broodwar->self()->getRace()) {
				if (BWAPI::Broodwar->self()->getUpgradeLevel(t)) {
					//BWAPI::Broodwar->printf("PUSHED: %s\n", t.getName());
					goal.push_back(MetaPair(t, 1));
				}
				iunit++;
			}

		}

		if (goal.empty()) { //if a goal has not been made, make some zealots.
			goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Zealot, numZealots + 8));
			if (numNexusAll >= 2)
			{
				goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Dragoon, numDragoons + 4));
			}
		}
	}
    else if (Config::Strategy::StrategyName == "Protoss_ZealotRush")
    {
        goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Zealot, numZealots + 8));

        // once we have a 2nd nexus start making dragoons
        if (numNexusAll >= 2)
        {
            goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Dragoon, numDragoons + 4));
        }
    }
    else if (Config::Strategy::StrategyName == "Protoss_DragoonRush")
    {
        goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Dragoon, numDragoons + 6));
    }
    else if (Config::Strategy::StrategyName == "Protoss_Drop")
    {
        if (numZealots == 0)
        {
            goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Zealot, numZealots + 4));
            goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Shuttle, 1));
        }
        else
        {
            goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Zealot, numZealots + 8));
        }
    }
    else if (Config::Strategy::StrategyName == "Protoss_DTRush")
    {
        goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Dark_Templar, numDarkTeplar + 2));

        // if we have a 2nd nexus then get some goons out
        if (numNexusAll >= 2)
        {
            goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Dragoon, numDragoons + 4));
        }
	}
    else
    {
        UAB_ASSERT_WARNING(false, "Unknown Protoss Strategy Name: %s", Config::Strategy::StrategyName.c_str());
    }

    // if we have 3 nexus, make an observer
  /*  if (numNexusCompleted >= 3)
    {
        goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Observer, 1));
    }*/
    
    // add observer to the goal if the enemy has cloaked units
/*	if (InformationManager::Instance().enemyHasCloakedUnits())
	{
		goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Robotics_Facility, 1));
		
		if (BWAPI::Broodwar->self()->completedUnitCount(BWAPI::UnitTypes::Protoss_Robotics_Facility) > 0)
		{
			goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Observatory, 1));
		}
		if (BWAPI::Broodwar->self()->completedUnitCount(BWAPI::UnitTypes::Protoss_Observatory) > 0)
		{
			goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Observer, 1));
		}
	}

    // if we want to expand, insert a nexus into the build order
	if (shouldExpandNow())
	{
		goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Nexus, numNexusAll + 1));
	}*/

	return goal;
}

const MetaPairVector StrategyManager::getTerranBuildOrderGoal() const
{
	// the goal to return
	std::vector<MetaPair> goal;

    int numWorkers      = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_SCV);
    int numCC           = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Command_Center);            
    int numMarines      = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Marine);
	int numMedics       = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Medic);
	int numWraith       = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Wraith);
    int numVultures     = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Vulture);
    int numGoliath      = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Goliath);
    int numTanks        = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode)
                        + UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode);
    int numBay          = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Engineering_Bay);

    if (Config::Strategy::StrategyName == "Terran_MarineRush")
    {
	    goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Terran_Marine, numMarines + 8));

        if (numMarines > 5)
        {
            goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Terran_Engineering_Bay, 1));
        }
    }
    else if (Config::Strategy::StrategyName == "Terran_4RaxMarines")
    {
	    goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Terran_Marine, numMarines + 8));
    }
    else if (Config::Strategy::StrategyName == "Terran_VultureRush")
    {
        goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Terran_Vulture, numVultures + 8));

        if (numVultures > 8)
        {
            goal.push_back(std::pair<MetaType, int>(BWAPI::TechTypes::Tank_Siege_Mode, 1));
            goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode, 4));
        }
    }
    else if (Config::Strategy::StrategyName == "Terran_TankPush")
    {
        goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode, 6));
        goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Terran_Goliath, numGoliath + 6));
        goal.push_back(std::pair<MetaType, int>(BWAPI::TechTypes::Tank_Siege_Mode, 1));
    }
    else
    {
        BWAPI::Broodwar->printf("Warning: No build order goal for Terran Strategy: %s", Config::Strategy::StrategyName.c_str());
    }



    if (shouldExpandNow())
    {
        goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Terran_Command_Center, numCC + 1));
        goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Terran_SCV, numWorkers + 10));
    }

	return goal;
}

const MetaPairVector StrategyManager::getZergBuildOrderGoal() const
{
	// the goal to return
	std::vector<MetaPair> goal;
	
    int numWorkers      = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Drone);
    int numCC           = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hatchery)
                        + UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Lair)
                        + UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hive);
	int numMutas        = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Mutalisk);
    int numDrones       = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Drone);
    int zerglings       = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Zergling);
	int numHydras       = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hydralisk);
    int numScourge      = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Scourge);
    int numGuardians    = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Guardian);

	int mutasWanted = numMutas + 6;
	int hydrasWanted = numHydras + 6;

    if (Config::Strategy::StrategyName == "Zerg_ZerglingRush")
    {
        goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Zergling, zerglings + 6));
    }
    else if (Config::Strategy::StrategyName == "Zerg_2HatchHydra")
    {
        goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Hydralisk, numHydras + 8));
        goal.push_back(std::pair<MetaType, int>(BWAPI::UpgradeTypes::Grooved_Spines, 1));
        goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Drone, numDrones + 4));
    }
    else if (Config::Strategy::StrategyName == "Zerg_3HatchMuta")
    {
        goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Hydralisk, numHydras + 12));
        goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Drone, numDrones + 4));
    }
    else if (Config::Strategy::StrategyName == "Zerg_3HatchScourge")
    {
        if (numScourge > 40)
        {
            goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Hydralisk, numHydras + 12));
        }
        else
        {
            goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Scourge, numScourge + 12));
        }

        
        goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Drone, numDrones + 4));
    }

    if (shouldExpandNow())
    {
        goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Hatchery, numCC + 1));
        goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Drone, numWorkers + 10));
    }

	return goal;
}

void StrategyManager::readResults()
{
    if (!Config::Modules::UsingStrategyIO)
    {
        return;
    }

    std::string enemyName = BWAPI::Broodwar->enemy()->getName();
    std::replace(enemyName.begin(), enemyName.end(), ' ', '_');

    std::string enemyResultsFile = Config::Strategy::ReadDir + enemyName + ".txt";
    
    std::string strategyName;
    int wins = 0;
    int losses = 0;

    FILE *file = fopen ( enemyResultsFile.c_str(), "r" );
    if ( file != nullptr )
    {
        char line [ 4096 ]; /* or other suitable maximum line size */
        while ( fgets ( line, sizeof line, file ) != nullptr ) /* read a line */
        {
            std::stringstream ss(line);

            ss >> strategyName;
            ss >> wins;
            ss >> losses;

            //BWAPI::Broodwar->printf("Results Found: %s %d %d", strategyName.c_str(), wins, losses);

            if (_strategies.find(strategyName) == _strategies.end())
            {
                //BWAPI::Broodwar->printf("Warning: Results file has unknown Strategy: %s", strategyName.c_str());
            }
            else
            {
                _strategies[strategyName]._wins = wins;
                _strategies[strategyName]._losses = losses;
            }
        }

        fclose ( file );
    }
    else
    {
        //BWAPI::Broodwar->printf("No results file found: %s", enemyResultsFile.c_str());
    }
}

void StrategyManager::writeResults(bool isWinner)
{
    if (!Config::Modules::UsingStrategyIO)
    {
        return;
    }

    std::string enemyName = BWAPI::Broodwar->enemy()->getName();
    std::replace(enemyName.begin(), enemyName.end(), ' ', '_');

    std::string resultsFile = Config::Strategy::WriteDir + Config::BotInfo::BotName + "_Results.txt";

    std::stringstream ss;
	ss << Config::BotInfo::BotName << "," << BWAPI::Broodwar->self()->getRace() << "," << "UAlbertabot" << "," << BWAPI::Broodwar->enemy()->getRace() << "," << isWinner << ',' << static_cast<int>(BWAPI::Broodwar->getFrameCount()/23.8) << "," << openingGame << "," << Config::Tools::OpeningWindow << "," << Config::Tools::Window << "," << Config::Tools::RandomStartingStrat << "," << Config::Tools::wu << "," << Config::Tools::wb << "," << Config::Tools::wf << "," << Config::Tools::we << ",";
	ss << Config::Tools::fuc << "," << Config::Tools::fbc << "," << Config::Tools::euc << "," << Config::Tools::ebc << ",";

	for (auto it = rmse.begin(); it != rmse.end(); ++it) {
		double num = *it;
		ss << std::to_string(num);
		if (it + 1 != rmse.end()) {
			ss << ",";
		}
		else {
			ss << std::endl;
		}
	}

    /*for (auto & kv : _strategies)
    {
        const Strategy & strategy = kv.second;

        ss << strategy._name << " " << strategy._wins << " " << strategy._losses << "\n";
    }*/

    Logger::LogAppendToFile(resultsFile, ss.str());
}

void StrategyManager::onEnd(const bool isWinner)
{
    if (!Config::Modules::UsingStrategyIO)
    {
        return;
    }

    if (isWinner)
    {
        _strategies[Config::Strategy::StrategyName]._wins++;
    }
    else
    {
        _strategies[Config::Strategy::StrategyName]._losses++;
    }

    writeResults(isWinner);
}

void StrategyManager::setLearnedStrategy()
{
    // we are currently not using this functionality for the competition so turn it off 
    return;

    if (!Config::Modules::UsingStrategyIO)
    {
        return;
    }

    const std::string & strategyName = Config::Strategy::StrategyName;
    Strategy & currentStrategy = _strategies[strategyName];

    int totalGamesPlayed = 0;
    int strategyGamesPlayed = currentStrategy._wins + currentStrategy._losses;
    double winRate = strategyGamesPlayed > 0 ? currentStrategy._wins / static_cast<double>(strategyGamesPlayed) : 0;

    // if we are using an enemy specific strategy
    if (Config::Strategy::FoundEnemySpecificStrategy)
    {        
        return;
    }

    // if our win rate with the current strategy is super high don't explore at all
    // also we're pretty confident in our base strategies so don't change if insufficient games have been played
    if (strategyGamesPlayed < 5 || (strategyGamesPlayed > 0 && winRate > 0.49))
    {
        BWAPI::Broodwar->printf("Still using default strategy");
        return;
    }

    // get the total number of games played so far with this race
    for (auto & kv : _strategies)
    {
        Strategy & strategy = kv.second;
        if (strategy._race == BWAPI::Broodwar->self()->getRace())
        {
            totalGamesPlayed += strategy._wins + strategy._losses;
        }
    }

    // calculate the UCB value and store the highest
    double C = 0.5;
    std::string bestUCBStrategy;
    double bestUCBStrategyVal = std::numeric_limits<double>::lowest();
    for (auto & kv : _strategies)
    {
        Strategy & strategy = kv.second;
        if (strategy._race != BWAPI::Broodwar->self()->getRace())
        {
            continue;
        }

        int sGamesPlayed = strategy._wins + strategy._losses;
        double sWinRate = sGamesPlayed > 0 ? currentStrategy._wins / static_cast<double>(strategyGamesPlayed) : 0;
        double ucbVal = C * sqrt( log( (double)totalGamesPlayed / sGamesPlayed ) );
        double val = sWinRate + ucbVal;

        if (val > bestUCBStrategyVal)
        {
            bestUCBStrategy = strategy._name;
            bestUCBStrategyVal = val;
        }
    }

    Config::Strategy::StrategyName = bestUCBStrategy;
}