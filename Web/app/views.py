from django.shortcuts import render
from django.http import HttpResponse

# Create your views here.
def home(request):
    name = ""
    if request.method == 'GET':
        name = request.GET.get('name', name)
    return HttpResponse('Welcome <b style="color:#FF0000">{}</b> to <b><i>EasyRL</i></b> app. <br/>Try out <a href="http://localhost:8000/easyRL">Test Simple Page Here</a>'.format(name))

def main(request):
    return render(request, "main.html")
