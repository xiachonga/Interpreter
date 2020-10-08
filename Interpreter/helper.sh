docker restart compiler2.0

if [ "$1" = "u" ]
then
  shift
  docker cp ast-interpreter/ASTInterpreter.cpp compiler2.0:/root/AST_Interpreter
  docker cp ast-interpreter/Environment.h compiler2.0:/root/AST_Interpreter
  docker exec -t compiler2.0 make
  if [ $? -ne 0 ]
  then
    echo "\033[5m\033[31m \n>>>>> Make fail!\n \033[0m"
    exit 1
  fi
fi

total=0
pass=0

runtest(){
  for cfile in $*
  do
    if [ -e "test/$cfile" ]
    then
      echo "\033[32m>>>>> $cfile\033[0m"
      docker cp test/$cfile compiler2.0:/root/test/$cfile
      docker exec -t compiler2.0 /bin/bash run.sh $cfile
      if [ $? -eq 0 ]
      then
        pass=`expr $pass + 1`
      fi
      echo
      total=`expr $total + 1`
    else
      echo "\033[31m\n>>>>> $cfile: File dosen't exsit!\033[0m"
    fi
  done
}

if [ "$1" = "r" ]
then
  shift
  if [ "$1" = "all" ]
  then
    runtest `ls ./test | grep "test[0-1][1-9].c"`
    echo "\033[32m\n>>>>> Total: $total\n>>>>> Pass:  $pass\033[0m"
  else
    runtest $*
    echo
  fi
fi


