# Copyright 2015 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""The Python implementation of the GRPC assign4.Database server."""

from concurrent import futures
import logging

import grpc

import assign4_pb2
import assign4_pb2_grpc

# You can implement your own DB here.
dic = {'0':'I', '1':'like', '2': 'EE324'}

class Database(assign4_pb2_grpc.DatabaseServicer):

    def AccessDB(self, request, context):
        print ("req: " + request.req)
        try:
            response = dic[request.req]
            print ("res: " + response)
        except KeyError:
            print("KeyError")
            response = '\0'

        return assign4_pb2.Response(res=response)

def serve():
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=10))
    assign4_pb2_grpc.add_DatabaseServicer_to_server(Database(), server)
    server.add_insecure_port('[::]:50060')
    server.start()
    server.wait_for_termination()


if __name__ == '__main__':
    logging.basicConfig()
    serve()
