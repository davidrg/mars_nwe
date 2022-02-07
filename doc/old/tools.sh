#!/bin/sh

yesno()
{
  echo ""
  echo $@
  while true; do
    echo "please answer with (Y)es or (N)o and <Return>."
    read X
    case "$X" in
     ('y'|'Y')
      return 0
     ;;
     ('n'|'N')
      return 1
     ;;
     *)
     ;;
    esac
  done
}

COMMAND=$1
shift

case "$COMMAND" in
 'yesno')
    if yesno $@ ; then exit 0; fi
  ;;

esac

exit 1
