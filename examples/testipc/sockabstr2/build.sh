# Compile server as "myapp"
g++ -std=c++20 -Wall -O2 server.cpp -o myapp -lpthread  && echo "ok..." || exit

# Compile client
g++ -std=c++20 -Wall -O2 client.cpp -o client -lpthread && echo "ok..." || exit

echo "ALL COMPLETE"

