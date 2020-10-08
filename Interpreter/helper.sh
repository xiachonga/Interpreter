docker restart compiler2.0

if [ "$1" = "u" ]
then
  shift
  docker cp ast-interpreter/ASTInterpreter.cpp compiler2.0:/root/AST_Interpreter
  docker cp ast-interpreter/Environment.h compiler2.0:/root/AST_Interpreter
  docker exec -t compiler2.0 make
fi
if [ $? -ne 0 ]
then
  echo "\033[5m\033[31m \n>>>>> Make fail!\n \033[0m"
  exit 1
fi
if [ "$1" = "r" ]
then
  shift
  if [ "$1" = "all" ]
  then
    echo "\033[5m\033[31m \n>>>>> TODO run all! \033[0m"
  else
    for cfile in $*
    do
      echo "\033[32m >>>>> $cfile \033[0m"
      docker exec -t compiler2.0 /bin/bash run.sh $cfile
      echo
    done
  fi
  echo
fi


