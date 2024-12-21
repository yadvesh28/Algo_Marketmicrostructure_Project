#!/bin/bash

#Check if the required arguments are provided
if [ $# -lt 5 ]; then
    echo "Please provide the PHASE, RUN_NUM, START_DATE, END_DATE, DELETE_INSTANCE"
    echo "Usage: $0 PHASE RUN_NUM START_DATE END_DATE DELETE_INSTANCE"
    echo "Example: $0 Initial 3 2021-11-05 2021-11-05 0"
    exit 1
fi

# Get the RUN_NUM and PHASE from the command-line arguments
PHASE=$1
RUN_NUM=$2
START_DATE=$3
END_DATE=$4
IS_DELETE_TRUE=$5

# Convert the dates to the desired string format
START_DATE_FORMATTED=$(date -d "$START_DATE" +"%Y-%m-%d")
END_DATE_FORMATTED=$(date -d "$END_DATE" +"%Y-%m-%d")

# Use the RUN_NUM and PHASE variables in the script
echo "Running strategy for RUN_NUM: $RUN_NUM in PHASE: $PHASE"

cd /home/vagrant/ss/sdk/RCM/StrategyStudio/Stop Loss Liquidity Taking Strategy/v1
rm StopLossLiquidityTaking.o StopLossLiquidityTaking.so
rm /home/vagrant/ss/bt/strategies_dlls/StopLossLiquidityTaking.so

make
cp StopLossLiquidityTaking.so /home/vagrant/ss/bt/strategies_dlls


cd /home/vagrant/ss/bt/
./StrategyServerBacktesting &
sleep 1

cd /home/vagrant/ss/bt/utilities
./StrategyCommandLine cmd create_instance StopLossHunter_${PHASE}_$RUN_NUM StopLossHunter UIUC SIM-1001-101 dlariviere 1000000 -symbols "BA"
./StrategyCommandLine cmd strategy_instance_list

./StrategyCommandLine cmd start_backtest $START_DATE_FORMATTED $END_DATE_FORMATTED StopLossHunter_${PHASE}_$RUN_NUM 0

if [ $IS_DELETE_TRUE -eq 1 ]; then
    echo "Deleting the created backtesting instance..."
    ./StrategyCommandLine cmd terminate StopLossHunter_${PHASE}_$RUN_NUM 
    ./StrategyCommandLine cmd strategy_instance_list
fi

echo "Press Ctrl + C to terminate script."