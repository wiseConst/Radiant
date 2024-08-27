@echo off
set DOT_FILE="../build/render_graph_ref.dot"
set OUTPUT_FILE=render_graph_ref.png

rem Path to Graphviz bin directory
set GRAPHVIZ_BIN="../ThirdParty\graphviz\Graphviz-12.1.0-win64\bin"

rem Check if the DOT file exists
if not exist %DOT_FILE% (
    echo DOT file not found!
    exit /b
)

rem Check if Graphviz bin directory exists
if not exist %GRAPHVIZ_BIN% (
    echo Graphviz bin directory not found!
    exit /b
)

rem Generate the graph image
%GRAPHVIZ_BIN%\dot -Tpng %DOT_FILE% -o %OUTPUT_FILE%

rem Check if the output file was generated successfully
if not exist %OUTPUT_FILE% (
    echo Graph generation failed!
    exit /b
)