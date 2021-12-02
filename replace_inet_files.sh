rsync -abuvP --include="*/" --include="*.cc" --include="*.h" --include="*.ned"  --exclude="*"  inet/  $1/src/inet/ 
find $1/src/inet -name "*.*~" -delete
