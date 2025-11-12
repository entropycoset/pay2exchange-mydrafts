# Compile server
g++ -std=c++17 -Wall -O2 server.cpp -o server -lpthread  && echo "ok..." || exit

# Compile client
g++ -std=c++17 -Wall -O2 client.cpp -o client -lpthread && echo "ok..." || exit

echo "ALL COMPLETE"

