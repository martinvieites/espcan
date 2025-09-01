FROM python:latest

WORKDIR /esp32can

ADD app.py .
ADD esp32can.py .
ADD templates/ templates/
ADD dbc/ dbc/
ADD dtc/ dtc/
ADD requirements.txt .

RUN pip install -r requirements.txt

EXPOSE 5000/tcp

CMD ["python", "app.py"]
