#pragma once

#include "Common.h"
#include "BWTA.h"
#include "BuildOrderQueue.h"
#include "InformationManager.h"
#include "WorkerManager.h"
#include "BuildOrder.h"

namespace UAlbertaBot
{
typedef std::pair<MetaType, size_t> MetaPair;
typedef std::vector<MetaPair> MetaPairVector;

struct Strategy
{
    std::string _name;
    BWAPI::Race _race;
    int         _wins;
    int         _losses;
    BuildOrder  _buildOrder;

    Strategy()
        : _name("None")
        , _race(BWAPI::Races::None)
        , _wins(0)
        , _losses(0)
    {
    
    }

    Strategy(const std::string & name, const BWAPI::Race & race, const BuildOrder & buildOrder)
        : _name(name)
        , _race(race)
        , _wins(0)
        , _losses(0)
        , _buildOrder(buildOrder)
    {
    
    }
};

class StrategyManager 
{
	StrategyManager();

	BWAPI::Race					    _selfRace;
	BWAPI::Race					    _enemyRace;
    std::map<std::string, Strategy> _strategies;
    int                             _totalGamesPlayed;
    const BuildOrder                _emptyBuildOrder;
	std::ifstream					dbFile;
	std::vector<std::vector<int>>	pastStates; //pastStates[i] = a past state. pastStates[i][j] = unit in that state.
	std::vector<std::string>		stateMoves;
	std::vector<int>				estimatedState;
	std::vector<int>				prevPredictedState;
	std::vector<double>				rmse;
	
	//following represent positions in a state retrieved from the prediction database
	unsigned int _gameNum; //Game number/id
	int _fucs; //friendly unit count start
	int	_fuce; //friendly unit count end
	int	_fbcs; //friendly building count start
	int	_fbce; //friendly building count end
	int	_fups; //friendly building count start
	int	_fupe; //friendly building count end
	int	_eucs; //enemy unit count start
	int	_euce; //enemy unit count end
	int	_ebcs; //enemy building count start
	int	_ebce; //enemy building count end
	int	_eups; //enemy unit count start
	int	_eupe; //enemy unit count end


	int numGames;
	int openingGame;

	std::string goalStateStr;
	std::string bestStateStr;
	std::string estimatedStateStr;
	int bestState;
	int goalState;

	        void	                writeResults(bool isWinner);
	const	int					    getScore(BWAPI::Player player) const;
	const	double				    getUCBValue(const size_t & strategy) const;
	const	bool				    shouldExpandNow() const;
    const	MetaPairVector		    getProtossBuildOrderGoal() const;
	const	MetaPairVector		    getTerranBuildOrderGoal() const;
	const	MetaPairVector		    getZergBuildOrderGoal() const;

public:
    
	static	StrategyManager &	    Instance();

			void				    onEnd(const bool isWinner);
            void                    addStrategy(const std::string & name, Strategy & strategy);
            void                    setLearnedStrategy();
            void	                readResults();
			BuildOrder 				predict();
			std::vector<std::string> split(const std::string &s, char delim);
			double					eucDistance(const double lhs, const double rhs);
			BuildOrder				selectOpeningStrategy();
			BuildOrder 				getBuildOrderFromState(int bestState, int window);
			double					calcStateDist(std::vector<int> state);
			void					calcError();
			
	const	bool				    regroup(int numInRadius);
	const	bool				    rushDetected();
	const	int				        defendWithWorkers();
	const	MetaPairVector		    getBuildOrderGoal();
	const	BuildOrder &            getOpeningBookBuildOrder() const;
	void							drawPrediction();
};
}