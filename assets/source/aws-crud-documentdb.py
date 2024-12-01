import json
import boto3
from decimal import Decimal

client = boto3.client('dynamodb')
dynamodb = boto3.resource("dynamodb")
table = dynamodb.Table('http-crud-tutorial-items')
tablesetpoint = 'http-crud-tutorial-items'


def lambda_handler(event, context):
    print(event)
    body = {}
    statusCode = 200
    headers = {
        "Content-Type": "application/json"
    }

    try:
        if event['routeKey'] == "DELETE /items/{date}":
            table.delete_item(
                Key={'date': event['pathParameters']['date']})
            body = 'Deleted item ' + event['pathParameters']['date']
        elif event['routeKey'] == "GET /items/{date}":
            body = table.get_item(
                Key={'date': event['pathParameters']['date']})
            body = body["Item"]
            responseBody = [
                {'temp': float(body['temp']), 'date': body['date'], 'setpoint': body['setpoint']}]
            body = responseBody
        elif event['routeKey'] == "GET /items":
            body = table.scan()
            body = body["Items"]
            print("ITEMS----")
            print(body)
            responseBody = []
            for items in body:
                responseItems = [
                    {'temp': float(items['temp']), 'date': items['date'], 'setpoint': items['setpoint']}]
                responseBody.append(responseItems)
            body = responseBody
        elif event['routeKey'] == "PUT /items":
            requestJSON = json.loads(event['body'])
            table.put_item(
                Item={
                    'date': requestJSON['date'],
                    'temp': Decimal(str(requestJSON['temp'])),
                    'setpoint': requestJSON['setpoint']
                })
            body = 'Put item ' + requestJSON['date']
    except KeyError:
        statusCode = 400
        body = 'Unsupported route: ' + event['routeKey']
    body = json.dumps(body)
    res = {
        "statusCode": statusCode,
        "headers": {
            "Content-Type": "application/json"
        },
        "body": body
    }
    return res
